# Spec 002 (plan §3, §5.14): the graphical launcher `ayther_replay_qa`. The window is a
# thin SDL3 + Dear ImGui adapter. It links the supervisor library and never Runtime src/.
find_package(tomlplusplus 3.4.0 EXACT CONFIG REQUIRED)

add_library(ayther_replay_qa_model STATIC
    environment_resolver.cpp launcher_messages.cpp launcher_model.cpp launcher_runner.cpp
    launcher_view.cpp option_correspondence.cpp)
target_include_directories(ayther_replay_qa_model PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(ayther_replay_qa_model PUBLIC cxx_std_20)
target_link_libraries(ayther_replay_qa_model
    PUBLIC ayther_audio_qa_cli
    PRIVATE tomlplusplus::tomlplusplus)
if(DEFINED AYTHER_RUNTIME_GENERATED_DIR)
    # Spec 002 (RF-1.8): the correspondence names the delivered Runtime version.
    target_include_directories(ayther_replay_qa_model PRIVATE "${AYTHER_RUNTIME_GENERATED_DIR}")
    target_compile_definitions(ayther_replay_qa_model PRIVATE
        AYTHER_REPLAY_QA_HAS_RUNTIME_VERSION=1)
endif()
set_target_properties(ayther_replay_qa_model PROPERTIES CXX_EXTENSIONS OFF)

add_executable(ayther_replay_qa WIN32 launcher_app.cpp)
target_link_libraries(ayther_replay_qa PRIVATE ayther_replay_qa_model SDL3::SDL3 imgui::imgui)
target_compile_features(ayther_replay_qa PRIVATE cxx_std_20)
set_target_properties(ayther_replay_qa PROPERTIES
    CXX_EXTENSIONS OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

install(TARGETS ayther_replay_qa
    RUNTIME_DEPENDENCIES
        PRE_EXCLUDE_REGEXES
            "api-ms-.*"
            "ext-ms-.*"
        POST_EXCLUDE_REGEXES
            ".*[\\/][Ss][Yy][Ss][Tt][Ee][Mm]32[\\/].*"
            "^/lib/.*"
            "^/usr/lib/.*"
    RUNTIME DESTINATION bin COMPONENT qa
)

# Spec 002 (BR-157): the window opens and draws; needs a desktop session and a GPU.
if(AYTHER_ENABLE_GPU_TESTS)
    add_test(NAME replay_qa_launcher_window_smoke
        COMMAND ayther_replay_qa --smoke-frames 30)
    set_tests_properties(replay_qa_launcher_window_smoke PROPERTIES
        LABELS "qa;gpu" TIMEOUT 60 PASS_REGULAR_EXPRESSION "smoke frames=30")
    # Spec 002, BR-172 (RF-1.1, RF-2.1): the launcher opens, shows the errors of the required
    # fields, starts a visible request with the synthetic core, cancels it and shows the result.
    if(TARGET ayther_runtime AND TARGET synthetic_libretro_core)
        add_test(NAME replay_qa_launcher_interface_smoke
            COMMAND ayther_replay_qa --self-test
                --runtime "$<TARGET_FILE:ayther_runtime>"
                --core "$<TARGET_FILE:synthetic_libretro_core>"
                --rom "${PROJECT_SOURCE_DIR}/tests/audio_qa/fixtures/public-synthetic.aytest"
                --take "${CMAKE_BINARY_DIR}/tests/audio_qa/fixtures/$<CONFIG>/public-synthetic-long.arp"
                --output "${CMAKE_CURRENT_BINARY_DIR}/interface-smoke/$<CONFIG>")
        set_tests_properties(replay_qa_launcher_interface_smoke PROPERTIES
            LABELS "qa;gpu;launcher" TIMEOUT 90 RUN_SERIAL TRUE
            PASS_REGULAR_EXPRESSION "ayther_replay_qa: self_test passed"
            FIXTURES_REQUIRED audio_qa_public_recording
            ENVIRONMENT "APPDATA=${CMAKE_CURRENT_BINARY_DIR}/interface-smoke/appdata")
    endif()
endif()

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../tests/replay_qa_launcher"
    "${CMAKE_CURRENT_BINARY_DIR}/tests")
