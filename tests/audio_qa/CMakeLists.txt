cmake_minimum_required(VERSION 3.25)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(AytherAudioQaFixture LANGUAGES CXX)
    enable_testing()
endif()

if(AYTHER_CLANG_TIDY_COMMAND)
    set(CMAKE_CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()

add_executable(audio_qa_campaign_audit_test campaign_audit_test.cpp)
target_link_libraries(audio_qa_campaign_audit_test PRIVATE
    ayther_audio_qa_cli ayther_audio_qa_evidence ayther_audio_qa_model_toml)
target_compile_features(audio_qa_campaign_audit_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_campaign_audit_test PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(audio_qa_campaign_audit_test PRIVATE /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_campaign_audit_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_campaign_audit COMMAND audio_qa_campaign_audit_test)
set_tests_properties(audio_qa_campaign_audit PROPERTIES
    LABELS "audio_qa;campaign;evidence;integration" TIMEOUT 15)

if(AYTHER_REQUIRE_AUDIO_QA_ENGINE_PACKAGE)
    get_target_property(qa_engine_include_directories
        Ayther::engine INTERFACE_INCLUDE_DIRECTORIES)
    if(NOT qa_engine_include_directories)
        message(FATAL_ERROR
            "Ayther::engine does not export public include directories")
    endif()
    foreach(include_directory IN LISTS qa_engine_include_directories)
        file(TO_CMAKE_PATH "${include_directory}" normalized_include_directory)
        if(normalized_include_directory MATCHES "/AYTHER-Engine/(src|include)$")
            message(FATAL_ERROR
                "Audio QA must consume an installed Engine package, not "
                "the Engine source tree: ${include_directory}")
        endif()
    endforeach()

    add_executable(audio_qa_engine_package_contract
        engine_package_contract_test.cpp)
    target_link_libraries(audio_qa_engine_package_contract PRIVATE Ayther::engine)
    target_compile_features(audio_qa_engine_package_contract PRIVATE cxx_std_20)
    set_target_properties(audio_qa_engine_package_contract PROPERTIES
        CXX_EXTENSIONS OFF
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    add_test(NAME audio_qa_engine_package_contract
        COMMAND audio_qa_engine_package_contract)
    set_tests_properties(audio_qa_engine_package_contract PROPERTIES
        LABELS "audio_qa;contract" TIMEOUT 10)

    add_executable(audio_qa_observation_bridge_test
        observation_bridge_test.cpp)
    target_link_libraries(audio_qa_observation_bridge_test PRIVATE
        ayther_audio_qa_observation_bridge)
    target_compile_features(audio_qa_observation_bridge_test PRIVATE cxx_std_20)
    set_target_properties(audio_qa_observation_bridge_test PROPERTIES
        CXX_EXTENSIONS OFF)
    if(AYTHER_CLANG_TIDY_COMMAND)
        set_target_properties(audio_qa_observation_bridge_test PROPERTIES
            CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
    endif()
    if(MSVC)
        target_compile_options(audio_qa_observation_bridge_test PRIVATE
            /W4 /WX /permissive- /EHsc)
    else()
        target_compile_options(audio_qa_observation_bridge_test PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME audio_qa_observation_bridge
        COMMAND audio_qa_observation_bridge_test)
    set_tests_properties(audio_qa_observation_bridge PROPERTIES
        LABELS "audio_qa;contract;integration" TIMEOUT 10)

    add_executable(audio_qa_fact_throughput_test
        fact_throughput_test.cpp)
    target_link_libraries(audio_qa_fact_throughput_test PRIVATE
        ayther_audio_qa_observation_bridge ayther_audio_qa_model_toml)
    target_compile_features(audio_qa_fact_throughput_test PRIVATE cxx_std_20)
    set_target_properties(audio_qa_fact_throughput_test PROPERTIES
        CXX_EXTENSIONS OFF)
    if(AYTHER_CLANG_TIDY_COMMAND)
        set_target_properties(audio_qa_fact_throughput_test PROPERTIES
            CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
    endif()
    if(MSVC)
        target_compile_options(audio_qa_fact_throughput_test PRIVATE
            /W4 /WX /permissive- /EHsc)
    else()
        target_compile_options(audio_qa_fact_throughput_test PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME audio_qa_fact_throughput
        COMMAND audio_qa_fact_throughput_test)
    set_tests_properties(audio_qa_fact_throughput PROPERTIES
        LABELS "audio_qa;integration;limits" TIMEOUT 70)

    add_executable(audio_qa_fact_burst_limits_test
        fact_burst_limits_test.cpp)
    target_link_libraries(audio_qa_fact_burst_limits_test PRIVATE
        ayther_audio_qa_observation_bridge ayther_audio_qa_model_toml)
    target_compile_features(audio_qa_fact_burst_limits_test PRIVATE cxx_std_20)
    set_target_properties(audio_qa_fact_burst_limits_test PROPERTIES
        CXX_EXTENSIONS OFF)
    if(AYTHER_CLANG_TIDY_COMMAND)
        set_target_properties(audio_qa_fact_burst_limits_test PROPERTIES
            CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
    endif()
    if(MSVC)
        target_compile_options(audio_qa_fact_burst_limits_test PRIVATE
            /W4 /WX /permissive- /EHsc)
    else()
        target_compile_options(audio_qa_fact_burst_limits_test PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME audio_qa_fact_burst_limits
        COMMAND audio_qa_fact_burst_limits_test)
    set_tests_properties(audio_qa_fact_burst_limits PROPERTIES
        LABELS "audio_qa;integration;limits" TIMEOUT 10)

    add_executable(audio_qa_private_frame_instrumentation_cost
        private_frame_instrumentation_cost.cpp)
    target_link_libraries(audio_qa_private_frame_instrumentation_cost PRIVATE
        ayther_audio_qa_engine_replay ayther_audio_qa_observation_bridge)
    target_compile_features(audio_qa_private_frame_instrumentation_cost PRIVATE
        cxx_std_20)
    set_target_properties(audio_qa_private_frame_instrumentation_cost PROPERTIES
        CXX_EXTENSIONS OFF)
    if(AYTHER_CLANG_TIDY_COMMAND)
        set_target_properties(audio_qa_private_frame_instrumentation_cost PROPERTIES
            CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
    endif()
    if(MSVC)
        target_compile_options(audio_qa_private_frame_instrumentation_cost PRIVATE
            /W4 /WX /permissive- /EHsc)
    else()
        target_compile_options(audio_qa_private_frame_instrumentation_cost PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()

    if(WIN32)
        add_executable(audio_qa_memory_budget_test memory_budget_test.cpp)
        target_link_libraries(audio_qa_memory_budget_test PRIVATE
            ayther_audio_qa_observation_bridge Psapi)
        target_compile_features(audio_qa_memory_budget_test PRIVATE cxx_std_20)
        set_target_properties(audio_qa_memory_budget_test PROPERTIES
            CXX_EXTENSIONS OFF)
        if(AYTHER_CLANG_TIDY_COMMAND)
            set_target_properties(audio_qa_memory_budget_test PROPERTIES
                CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
        endif()
        if(MSVC)
            target_compile_options(audio_qa_memory_budget_test PRIVATE
                /W4 /WX /permissive- /EHsc)
        else()
            target_compile_options(audio_qa_memory_budget_test PRIVATE
                -Wall -Wextra -Wpedantic -Werror)
        endif()
        add_test(NAME audio_qa_memory_budget
            COMMAND audio_qa_memory_budget_test)
        set_tests_properties(audio_qa_memory_budget PROPERTIES
            LABELS "audio_qa;integration;limits;performance" TIMEOUT 20)
    endif()

    add_executable(audio_qa_engine_game_state_restore_test
        engine_game_state_restore_test.cpp)
    target_link_libraries(audio_qa_engine_game_state_restore_test PRIVATE
        ayther_audio_qa_engine_replay)
    target_compile_features(audio_qa_engine_game_state_restore_test PRIVATE
        cxx_std_20)
    set_target_properties(audio_qa_engine_game_state_restore_test PROPERTIES
        CXX_EXTENSIONS OFF)
    if(AYTHER_CLANG_TIDY_COMMAND)
        set_target_properties(audio_qa_engine_game_state_restore_test PROPERTIES
            CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
    endif()
    if(MSVC)
        target_compile_options(audio_qa_engine_game_state_restore_test PRIVATE
            /W4 /WX /permissive- /EHsc)
    else()
        target_compile_options(audio_qa_engine_game_state_restore_test PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME audio_qa_engine_game_state_restore
        COMMAND audio_qa_engine_game_state_restore_test)
    set_tests_properties(audio_qa_engine_game_state_restore PROPERTIES
        LABELS "audio_qa;contract;integration" TIMEOUT 10)

    add_executable(audio_qa_engine_hd_initialization_test
        engine_hd_initialization_test.cpp)
    target_link_libraries(audio_qa_engine_hd_initialization_test PRIVATE
        ayther_audio_qa_engine_replay)
    target_compile_features(audio_qa_engine_hd_initialization_test PRIVATE
        cxx_std_20)
    set_target_properties(audio_qa_engine_hd_initialization_test PROPERTIES
        CXX_EXTENSIONS OFF)
    if(AYTHER_CLANG_TIDY_COMMAND)
        set_target_properties(audio_qa_engine_hd_initialization_test PROPERTIES
            CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
    endif()
    if(MSVC)
        target_compile_options(audio_qa_engine_hd_initialization_test PRIVATE
            /W4 /WX /permissive- /EHsc)
    else()
        target_compile_options(audio_qa_engine_hd_initialization_test PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME audio_qa_engine_hd_initialization
        COMMAND audio_qa_engine_hd_initialization_test)
    set_tests_properties(audio_qa_engine_hd_initialization PROPERTIES
        LABELS "audio_qa;contract;integration" TIMEOUT 10)

    add_executable(audio_qa_engine_recording_replay_test
        engine_recording_replay_test.cpp)
    target_link_libraries(audio_qa_engine_recording_replay_test PRIVATE
        ayther_audio_qa_engine_replay)
    target_compile_features(audio_qa_engine_recording_replay_test PRIVATE
        cxx_std_20)
    set_target_properties(audio_qa_engine_recording_replay_test PROPERTIES
        CXX_EXTENSIONS OFF)
    if(AYTHER_CLANG_TIDY_COMMAND)
        set_target_properties(audio_qa_engine_recording_replay_test PROPERTIES
            CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
    endif()
    if(MSVC)
        target_compile_options(audio_qa_engine_recording_replay_test PRIVATE
            /W4 /WX /permissive- /EHsc)
    else()
        target_compile_options(audio_qa_engine_recording_replay_test PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME audio_qa_engine_recording_replay
        COMMAND audio_qa_engine_recording_replay_test)
    set_tests_properties(audio_qa_engine_recording_replay PROPERTIES
        LABELS "audio_qa;contract;integration" TIMEOUT 10)

    add_executable(audio_qa_engine_replay_production_close_test
        engine_replay_production_close_test.cpp)
    target_link_libraries(audio_qa_engine_replay_production_close_test PRIVATE
        ayther_audio_qa_engine_replay)
    target_compile_features(audio_qa_engine_replay_production_close_test PRIVATE
        cxx_std_20)
    set_target_properties(audio_qa_engine_replay_production_close_test PROPERTIES
        CXX_EXTENSIONS OFF)
    if(AYTHER_CLANG_TIDY_COMMAND)
        set_target_properties(audio_qa_engine_replay_production_close_test PROPERTIES
            CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
    endif()
    if(MSVC)
        target_compile_options(audio_qa_engine_replay_production_close_test PRIVATE
            /W4 /WX /permissive- /EHsc)
    else()
        target_compile_options(audio_qa_engine_replay_production_close_test PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME audio_qa_engine_replay_production_close
        COMMAND audio_qa_engine_replay_production_close_test)
    set_tests_properties(audio_qa_engine_replay_production_close PROPERTIES
        LABELS "audio_qa;contract;integration" TIMEOUT 10)
endif()

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../tools/audio_qa"
    "${CMAKE_CURRENT_BINARY_DIR}/domain")
add_test(NAME audio_qa_check_entry
    COMMAND ${CMAKE_COMMAND}
        "-DCHECK_EXE=$<TARGET_FILE:ayther_audio_qa>"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/check-entry/$<CONFIG>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_entry_test.cmake")
set_tests_properties(audio_qa_check_entry PROPERTIES
    LABELS "audio_qa;cli;integration" TIMEOUT 10)
add_executable(audio_qa_check_options_test check_options_test.cpp)
target_link_libraries(audio_qa_check_options_test PRIVATE ayther_audio_qa_cli)
target_compile_features(audio_qa_check_options_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_check_options_test PROPERTIES CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_check_options_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_check_options_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_check_options_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_check_options COMMAND audio_qa_check_options_test)
set_tests_properties(audio_qa_check_options PROPERTIES
    LABELS "audio_qa;cli;unit" TIMEOUT 10)
add_executable(audio_qa_check_option_descriptors_test check_option_descriptors_test.cpp)
target_link_libraries(audio_qa_check_option_descriptors_test PRIVATE
    ayther_audio_qa_cli tomlplusplus::tomlplusplus)
target_compile_features(audio_qa_check_option_descriptors_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_check_option_descriptors_test PROPERTIES CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_check_option_descriptors_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_check_option_descriptors_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_check_option_descriptors_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
if(WIN32)
    find_file(qa_descriptor_toml_dll_release NAMES tomlplusplus-3.dll
        PATHS "${tomlplusplus_DIR}/../../bin" NO_DEFAULT_PATH REQUIRED)
    find_file(qa_descriptor_toml_dll_debug NAMES tomlplusplus-3.dll
        PATHS "${tomlplusplus_DIR}/../../debug/bin" NO_DEFAULT_PATH REQUIRED)
    add_custom_command(TARGET audio_qa_check_option_descriptors_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${qa_descriptor_toml_dll_debug},${qa_descriptor_toml_dll_release}>"
            "$<TARGET_FILE_DIR:audio_qa_check_option_descriptors_test>" VERBATIM)
endif()
add_test(NAME audio_qa_check_option_descriptors
    COMMAND audio_qa_check_option_descriptors_test
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/rf1-option-inventory-beta8.toml"
        "$<TARGET_FILE:ayther_audio_qa>")
set_tests_properties(audio_qa_check_option_descriptors PROPERTIES
    LABELS "audio_qa;cli;unit;contract" TIMEOUT 10)
add_executable(audio_qa_take_plan_test take_plan_test.cpp)
target_link_libraries(audio_qa_take_plan_test PRIVATE ayther_audio_qa_cli)
target_compile_features(audio_qa_take_plan_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_take_plan_test PROPERTIES CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_take_plan_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_take_plan_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_take_plan_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_take_plan COMMAND audio_qa_take_plan_test)
set_tests_properties(audio_qa_take_plan PROPERTIES
    LABELS "audio_qa;cli;unit" TIMEOUT 10)
add_executable(audio_qa_material_preflight_test material_preflight_test.cpp)
target_link_libraries(audio_qa_material_preflight_test PRIVATE ayther_audio_qa_cli)
target_compile_features(audio_qa_material_preflight_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_material_preflight_test PROPERTIES CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_material_preflight_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_material_preflight_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_material_preflight_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_material_preflight COMMAND audio_qa_material_preflight_test)
set_tests_properties(audio_qa_material_preflight PROPERTIES
    LABELS "audio_qa;cli;unit" TIMEOUT 30)
add_executable(audio_qa_take_preflight_test take_preflight_test.cpp)
target_link_libraries(audio_qa_take_preflight_test PRIVATE ayther_audio_qa_cli)
target_compile_features(audio_qa_take_preflight_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_take_preflight_test PROPERTIES CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_take_preflight_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_take_preflight_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_take_preflight_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_take_preflight COMMAND audio_qa_take_preflight_test)
set_tests_properties(audio_qa_take_preflight PROPERTIES
    LABELS "audio_qa;cli;unit" TIMEOUT 30)
add_executable(audio_qa_request_summary_store_test request_summary_store_test.cpp)
target_link_libraries(audio_qa_request_summary_store_test PRIVATE ayther_audio_qa_cli)
target_compile_features(audio_qa_request_summary_store_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_request_summary_store_test PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(audio_qa_request_summary_store_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_request_summary_store_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
if(WIN32)
    add_custom_command(TARGET audio_qa_request_summary_store_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${qa_descriptor_toml_dll_debug},${qa_descriptor_toml_dll_release}>"
            "$<TARGET_FILE_DIR:audio_qa_request_summary_store_test>" VERBATIM)
endif()
add_test(NAME audio_qa_request_summary_store COMMAND audio_qa_request_summary_store_test)
set_tests_properties(audio_qa_request_summary_store PROPERTIES
    LABELS "audio_qa;cli;unit" TIMEOUT 10)
add_executable(audio_qa_runtime_protocol_v11_test runtime_protocol_v11_test.cpp)
target_link_libraries(audio_qa_runtime_protocol_v11_test PRIVATE ayther_audio_qa_cli
    ayther_audio_qa_model_toml)
target_compile_features(audio_qa_runtime_protocol_v11_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_runtime_protocol_v11_test PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(audio_qa_runtime_protocol_v11_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_runtime_protocol_v11_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
if(WIN32)
    add_custom_command(TARGET audio_qa_runtime_protocol_v11_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${qa_descriptor_toml_dll_debug},${qa_descriptor_toml_dll_release}>"
            "$<TARGET_FILE_DIR:audio_qa_runtime_protocol_v11_test>" VERBATIM)
endif()
add_test(NAME audio_qa_runtime_protocol_v11 COMMAND audio_qa_runtime_protocol_v11_test)
set_tests_properties(audio_qa_runtime_protocol_v11 PROPERTIES
    LABELS "audio_qa;protocol;unit" TIMEOUT 10)
add_executable(audio_qa_inspection_evidence_test inspection_evidence_test.cpp)
target_link_libraries(audio_qa_inspection_evidence_test PRIVATE ayther_audio_qa_cli)
target_compile_features(audio_qa_inspection_evidence_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_inspection_evidence_test PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(audio_qa_inspection_evidence_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_inspection_evidence_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
if(WIN32)
    add_custom_command(TARGET audio_qa_inspection_evidence_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${qa_descriptor_toml_dll_debug},${qa_descriptor_toml_dll_release}>"
            "$<TARGET_FILE_DIR:audio_qa_inspection_evidence_test>" VERBATIM)
endif()
add_test(NAME audio_qa_inspection_evidence COMMAND audio_qa_inspection_evidence_test)
set_tests_properties(audio_qa_inspection_evidence PROPERTIES
    LABELS "audio_qa;evidence;unit" TIMEOUT 10)
add_executable(audio_qa_inspection_facts_test inspection_facts_test.cpp)
target_link_libraries(audio_qa_inspection_facts_test PRIVATE ayther_audio_qa_cli
    ayther_audio_qa_evidence ayther_audio_qa_model_toml ayther_audio_qa_transport)
target_compile_features(audio_qa_inspection_facts_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_inspection_facts_test PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(audio_qa_inspection_facts_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_inspection_facts_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
if(WIN32)
    add_custom_command(TARGET audio_qa_inspection_facts_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${qa_descriptor_toml_dll_debug},${qa_descriptor_toml_dll_release}>"
            "$<TARGET_FILE_DIR:audio_qa_inspection_facts_test>" VERBATIM)
endif()
add_test(NAME audio_qa_inspection_facts COMMAND audio_qa_inspection_facts_test)
set_tests_properties(audio_qa_inspection_facts PROPERTIES
    LABELS "audio_qa;evidence;unit" TIMEOUT 10)
add_executable(audio_qa_evidence_completeness_test evidence_completeness_test.cpp)
target_link_libraries(audio_qa_evidence_completeness_test PRIVATE ayther_audio_qa_cli)
target_compile_features(audio_qa_evidence_completeness_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_evidence_completeness_test PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(audio_qa_evidence_completeness_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_evidence_completeness_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_evidence_completeness COMMAND audio_qa_evidence_completeness_test)
set_tests_properties(audio_qa_evidence_completeness PROPERTIES
    LABELS "audio_qa;evidence;unit" TIMEOUT 10)
add_executable(audio_qa_evidence_schema_v11_test evidence_schema_v11_test.cpp)
target_link_libraries(audio_qa_evidence_schema_v11_test PRIVATE ayther_audio_qa_cli
    ayther_audio_qa_model_toml tomlplusplus::tomlplusplus)
target_compile_features(audio_qa_evidence_schema_v11_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_evidence_schema_v11_test PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(audio_qa_evidence_schema_v11_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_evidence_schema_v11_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
if(WIN32)
    add_custom_command(TARGET audio_qa_evidence_schema_v11_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${qa_descriptor_toml_dll_debug},${qa_descriptor_toml_dll_release}>"
            "$<TARGET_FILE_DIR:audio_qa_evidence_schema_v11_test>" VERBATIM)
endif()
add_test(NAME audio_qa_evidence_schema_v11 COMMAND audio_qa_evidence_schema_v11_test)
set_tests_properties(audio_qa_evidence_schema_v11 PROPERTIES
    LABELS "audio_qa;evidence;unit" TIMEOUT 10)
add_executable(audio_qa_request_terminal_arbiter_test request_terminal_arbiter_test.cpp)
target_link_libraries(audio_qa_request_terminal_arbiter_test PRIVATE ayther_audio_qa_cli)
target_compile_features(audio_qa_request_terminal_arbiter_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_request_terminal_arbiter_test PROPERTIES CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_request_terminal_arbiter_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_request_terminal_arbiter_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_request_terminal_arbiter_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_request_terminal_arbiter COMMAND audio_qa_request_terminal_arbiter_test)
set_tests_properties(audio_qa_request_terminal_arbiter PROPERTIES
    LABELS "audio_qa;cli;unit" TIMEOUT 10)
add_executable(audio_qa_runtime_probe_test runtime_probe_test.cpp)
target_link_libraries(audio_qa_runtime_probe_test PRIVATE ayther_audio_qa_cli)
target_compile_features(audio_qa_runtime_probe_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_runtime_probe_test PROPERTIES CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_runtime_probe_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_runtime_probe_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_runtime_probe_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_runtime_probe COMMAND audio_qa_runtime_probe_test)
set_tests_properties(audio_qa_runtime_probe PROPERTIES
    LABELS "audio_qa;cli;unit" TIMEOUT 10)
add_executable(audio_qa_json_fields_test json_fields_test.cpp)
target_link_libraries(audio_qa_json_fields_test PRIVATE ayther_audio_qa_cli)
target_compile_features(audio_qa_json_fields_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_json_fields_test PROPERTIES CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_json_fields_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_json_fields_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_json_fields_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_json_fields COMMAND audio_qa_json_fields_test)
set_tests_properties(audio_qa_json_fields PROPERTIES
    LABELS "audio_qa;cli;unit" TIMEOUT 10)
add_executable(audio_qa_effective_values_test effective_values_test.cpp)
target_link_libraries(audio_qa_effective_values_test PRIVATE ayther_audio_qa_cli)
target_compile_features(audio_qa_effective_values_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_effective_values_test PROPERTIES CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_effective_values_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_effective_values_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_effective_values_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
if(WIN32)
    add_custom_command(TARGET audio_qa_effective_values_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${qa_descriptor_toml_dll_debug},${qa_descriptor_toml_dll_release}>"
            "$<TARGET_FILE_DIR:audio_qa_effective_values_test>" VERBATIM)
endif()
add_test(NAME audio_qa_effective_values COMMAND audio_qa_effective_values_test)
set_tests_properties(audio_qa_effective_values PROPERTIES
    LABELS "audio_qa;cli;unit" TIMEOUT 10)
add_executable(audio_qa_check_admission_test check_admission_test.cpp)
target_link_libraries(audio_qa_check_admission_test PRIVATE ayther_audio_qa_cli)
target_compile_features(audio_qa_check_admission_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_check_admission_test PROPERTIES CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_check_admission_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_check_admission_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_check_admission_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_check_admission COMMAND audio_qa_check_admission_test)
set_tests_properties(audio_qa_check_admission PROPERTIES
    LABELS "audio_qa;cli;integration" TIMEOUT 15)
add_executable(audio_qa_check_cancellation_test check_cancellation_test.cpp)
target_link_libraries(audio_qa_check_cancellation_test PRIVATE
    ayther_audio_qa_cli)
target_compile_features(audio_qa_check_cancellation_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_check_cancellation_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_check_cancellation_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_check_cancellation_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_check_cancellation_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_check_cancellation
    COMMAND audio_qa_check_cancellation_test)
set_tests_properties(audio_qa_check_cancellation PROPERTIES
    LABELS "audio_qa;cli;unit" TIMEOUT 10)
add_executable(audio_qa_check_summary_test check_summary_test.cpp)
target_link_libraries(audio_qa_check_summary_test PRIVATE ayther_audio_qa_cli)
target_compile_features(audio_qa_check_summary_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_check_summary_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_check_summary_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_check_summary_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_check_summary_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_check_summary COMMAND audio_qa_check_summary_test)
set_tests_properties(audio_qa_check_summary PROPERTIES
    LABELS "audio_qa;cli;unit" TIMEOUT 10)
add_executable(audio_qa_check_messages_test check_messages_test.cpp)
target_link_libraries(audio_qa_check_messages_test PRIVATE ayther_audio_qa_cli)
target_compile_features(audio_qa_check_messages_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_check_messages_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_check_messages_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_check_messages_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_check_messages_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_check_messages COMMAND audio_qa_check_messages_test)
set_tests_properties(audio_qa_check_messages PROPERTIES
    LABELS "audio_qa;cli;unit" TIMEOUT 10)
add_executable(audio_qa_evidence_query_index_test
    evidence_query_index_test.cpp)
target_link_libraries(audio_qa_evidence_query_index_test PRIVATE
    ayther_audio_qa_cli)
target_compile_features(audio_qa_evidence_query_index_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_evidence_query_index_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_evidence_query_index_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_evidence_query_index_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_evidence_query_index_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_evidence_query_index
    COMMAND audio_qa_evidence_query_index_test)
set_tests_properties(audio_qa_evidence_query_index PROPERTIES
    LABELS "audio_qa;cli;query;unit" TIMEOUT 10)
add_test(NAME audio_qa_query_entry
    COMMAND ${CMAKE_COMMAND}
        "-DCHECK_EXE=$<TARGET_FILE:ayther_audio_qa>"
        "-DQUERY_INDEX=${CMAKE_CURRENT_SOURCE_DIR}/event_audio_query_index.toml"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/query_entry_test.cmake")
set_tests_properties(audio_qa_query_entry PROPERTIES
    LABELS "audio_qa;cli;query;integration" TIMEOUT 10)
add_executable(audio_qa_public_recording_fixture
    public_recording_fixture.cpp)
target_include_directories(audio_qa_public_recording_fixture PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(audio_qa_public_recording_fixture PRIVATE
    ayther_audio_qa_domain synthetic_libretro_core zstd::libzstd)
target_compile_features(audio_qa_public_recording_fixture PRIVATE cxx_std_20)
set_target_properties(audio_qa_public_recording_fixture PROPERTIES
    CXX_EXTENSIONS OFF)
if(WIN32)
    add_custom_command(TARGET audio_qa_public_recording_fixture POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:synthetic_libretro_core>"
            "$<TARGET_FILE_DIR:audio_qa_public_recording_fixture>"
        VERBATIM)
endif()
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_public_recording_fixture PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_public_recording_fixture PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_public_recording_fixture PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_public_recording_fixture
    COMMAND audio_qa_public_recording_fixture
        "${CMAKE_CURRENT_BINARY_DIR}/fixtures/$<CONFIG>/public-synthetic.arp")
set_tests_properties(audio_qa_public_recording_fixture PROPERTIES
    LABELS "audio_qa;fixture;integration" TIMEOUT 15
    FIXTURES_SETUP audio_qa_public_recording)
# Spec 002, BR-152 (RF-2.8): synthetic takes of N frames, among them the take of one frame.
foreach(frames IN ITEMS 1 6 600)
    add_test(NAME audio_qa_public_recording_fixture_${frames}
        COMMAND audio_qa_public_recording_fixture
            "${CMAKE_CURRENT_BINARY_DIR}/fixtures/$<CONFIG>/public-synthetic-${frames}.arp"
            --frames ${frames})
    set_tests_properties(audio_qa_public_recording_fixture_${frames} PROPERTIES
        LABELS "audio_qa;fixture" TIMEOUT 30
        PASS_REGULAR_EXPRESSION "public_recording_fixture: frames=${frames} "
        FIXTURES_SETUP audio_qa_public_recording)
endforeach()
add_test(NAME audio_qa_public_recording_fixture_zero
    COMMAND audio_qa_public_recording_fixture
        "${CMAKE_CURRENT_BINARY_DIR}/fixtures/$<CONFIG>/public-synthetic-0.arp" --frames 0)
set_tests_properties(audio_qa_public_recording_fixture_zero PROPERTIES
    LABELS "audio_qa;fixture" TIMEOUT 30 WILL_FAIL TRUE)
add_executable(audio_qa_public_pack_fixture public_pack_fixture.cpp)
target_link_libraries(audio_qa_public_pack_fixture PRIVATE Ayther::engine)
target_compile_features(audio_qa_public_pack_fixture PRIVATE cxx_std_20)
set_target_properties(audio_qa_public_pack_fixture PROPERTIES CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_public_pack_fixture PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_public_pack_fixture PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_public_pack_fixture PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_public_pack_fixture
    COMMAND audio_qa_public_pack_fixture
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/public-synthetic.ay"
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/public-synthetic-trust.toml")
set_tests_properties(audio_qa_public_pack_fixture PROPERTIES
    LABELS "audio_qa;fixture;integration;pack" TIMEOUT 15)

add_executable(audio_qa_replay_execution_result_test
    replay_execution_result_test.cpp)
target_link_libraries(audio_qa_replay_execution_result_test PRIVATE
    ayther_audio_qa_model_toml tomlplusplus::tomlplusplus)
target_compile_features(audio_qa_replay_execution_result_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_replay_execution_result_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_replay_execution_result_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_replay_execution_result_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_replay_execution_result_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_replay_execution_result
    COMMAND audio_qa_replay_execution_result_test)
set_tests_properties(audio_qa_replay_execution_result PROPERTIES
    LABELS "audio_qa;protocol;unit" TIMEOUT 10)

add_executable(audio_qa_integrated_evidence_test integrated_evidence_test.cpp)
target_link_libraries(audio_qa_integrated_evidence_test PRIVATE
    ayther_audio_qa_cli ayther_audio_qa_model_toml)
target_compile_features(audio_qa_integrated_evidence_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_integrated_evidence_test PROPERTIES CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_integrated_evidence_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_integrated_evidence_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_integrated_evidence_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_integrated_evidence COMMAND audio_qa_integrated_evidence_test)
set_tests_properties(audio_qa_integrated_evidence PROPERTIES
    LABELS "audio_qa;evidence;integration" TIMEOUT 20)

if(AYTHER_REQUIRE_AUDIO_QA_ENGINE_PACKAGE)
    add_executable(audio_qa_replay_trace_test replay_trace_test.cpp)
    target_link_libraries(audio_qa_replay_trace_test PRIVATE
        ayther_audio_qa_engine_replay)
    target_compile_features(audio_qa_replay_trace_test PRIVATE cxx_std_20)
    set_target_properties(audio_qa_replay_trace_test PROPERTIES
        CXX_EXTENSIONS OFF)
    if(AYTHER_CLANG_TIDY_COMMAND)
        set_target_properties(audio_qa_replay_trace_test PROPERTIES
            CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
    endif()
    if(MSVC)
        target_compile_options(audio_qa_replay_trace_test PRIVATE
            /W4 /WX /permissive- /EHsc)
    else()
        target_compile_options(audio_qa_replay_trace_test PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME audio_qa_replay_trace COMMAND audio_qa_replay_trace_test)
    set_tests_properties(audio_qa_replay_trace PROPERTIES
        LABELS "audio_qa;trace;unit" TIMEOUT 10)
endif()

if(AYTHER_REQUIRE_AUDIO_QA_ENGINE_PACKAGE AND WIN32)
    add_dependencies(ayther_audio_qa ayther_runtime
        audio_qa_public_recording_fixture synthetic_libretro_core)
    add_test(NAME audio_qa_runtime_capabilities
        COMMAND ${CMAKE_COMMAND}
            "-DRUNTIME_EXE=$<TARGET_FILE:ayther_runtime>"
            -DEXPECT_FULL=ON
            -P "${CMAKE_CURRENT_SOURCE_DIR}/runtime_capabilities_test.cmake")
    set_tests_properties(audio_qa_runtime_capabilities PROPERTIES
        LABELS "audio_qa;runtime;contract;integration" TIMEOUT 15)
    # Spec 002 (BR-060, BR-063, BR-067 to BR-069): run_check with the real Runtime, no GPU.
    add_test(NAME audio_qa_public_long_recording_fixture
        COMMAND audio_qa_public_recording_fixture
            "${CMAKE_CURRENT_BINARY_DIR}/fixtures/$<CONFIG>/public-synthetic-long.arp" --long)
    set_tests_properties(audio_qa_public_long_recording_fixture PROPERTIES
        LABELS "audio_qa;fixture" TIMEOUT 30 FIXTURES_SETUP audio_qa_public_recording)
    add_test(NAME audio_qa_public_broken_recording_fixture
        COMMAND audio_qa_public_recording_fixture
            "${CMAKE_CURRENT_BINARY_DIR}/fixtures/$<CONFIG>/public-synthetic-broken.arp" --broken)
    set_tests_properties(audio_qa_public_broken_recording_fixture PROPERTIES
        LABELS "audio_qa;fixture" TIMEOUT 30 FIXTURES_SETUP audio_qa_public_recording)
    add_executable(audio_qa_check_runner_integration_test check_runner_integration_test.cpp)
    target_link_libraries(audio_qa_check_runner_integration_test PRIVATE ayther_audio_qa_cli)
    target_compile_features(audio_qa_check_runner_integration_test PRIVATE cxx_std_20)
    set_target_properties(audio_qa_check_runner_integration_test PROPERTIES CXX_EXTENSIONS OFF)
    target_compile_options(audio_qa_check_runner_integration_test PRIVATE
        /W4 /WX /permissive- /EHsc)
    add_custom_command(TARGET audio_qa_check_runner_integration_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${qa_descriptor_toml_dll_debug},${qa_descriptor_toml_dll_release}>"
            "$<TARGET_FILE_DIR:audio_qa_check_runner_integration_test>" VERBATIM)
    add_dependencies(audio_qa_check_runner_integration_test ayther_runtime synthetic_libretro_core)
    add_test(NAME audio_qa_check_runner_integration
        COMMAND audio_qa_check_runner_integration_test
            "$<TARGET_FILE:ayther_runtime>"
            "$<TARGET_FILE:synthetic_libretro_core>"
            "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/public-synthetic.aytest"
            "${CMAKE_CURRENT_BINARY_DIR}/fixtures/$<CONFIG>/public-synthetic.arp"
            "${CMAKE_CURRENT_BINARY_DIR}/fixtures/$<CONFIG>/public-synthetic-long.arp"
            "${CMAKE_CURRENT_BINARY_DIR}/check-runner/$<CONFIG>")
    set_tests_properties(audio_qa_check_runner_integration PROPERTIES
        LABELS "audio_qa;cli;runtime;integration"
        TIMEOUT 240 FIXTURES_REQUIRED audio_qa_public_recording
        ENVIRONMENT
            "SDL_AUDIO_DRIVER=dummy;SDL_AUDIO_FREQUENCY=48000;SDL_AUDIO_CHANNELS=2;SDL_AUDIO_DEVICE_SAMPLE_FRAMES=512")
    # Spec 002 (BR-135): a synthetic supervisor speaks protocol 1.1 with the real Runtime.
    add_executable(audio_qa_runtime_session_v11_test runtime_session_v11_test.cpp)
    target_link_libraries(audio_qa_runtime_session_v11_test PRIVATE ayther_audio_qa_cli
        ayther_audio_qa_transport ayther_audio_qa_model_toml)
    target_compile_features(audio_qa_runtime_session_v11_test PRIVATE cxx_std_20)
    set_target_properties(audio_qa_runtime_session_v11_test PROPERTIES CXX_EXTENSIONS OFF)
    target_compile_options(audio_qa_runtime_session_v11_test PRIVATE /W4 /WX /permissive- /EHsc)
    add_custom_command(TARGET audio_qa_runtime_session_v11_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${qa_descriptor_toml_dll_debug},${qa_descriptor_toml_dll_release}>"
            "$<TARGET_FILE_DIR:audio_qa_runtime_session_v11_test>" VERBATIM)
    add_dependencies(audio_qa_runtime_session_v11_test ayther_runtime synthetic_libretro_core)
    add_test(NAME audio_qa_runtime_session_v11
        COMMAND audio_qa_runtime_session_v11_test
            "$<TARGET_FILE:ayther_runtime>"
            "$<TARGET_FILE:synthetic_libretro_core>"
            "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/public-synthetic.aytest"
            "${CMAKE_CURRENT_BINARY_DIR}/fixtures/$<CONFIG>/public-synthetic.arp"
            "${CMAKE_CURRENT_BINARY_DIR}/fixtures/$<CONFIG>/public-synthetic-long.arp"
            "${CMAKE_CURRENT_BINARY_DIR}/runtime-session-v11/$<CONFIG>")
    set_tests_properties(audio_qa_runtime_session_v11 PROPERTIES
        LABELS "audio_qa;runtime;protocol;integration"
        TIMEOUT 120 FIXTURES_REQUIRED audio_qa_public_recording)
    # Spec 002 (BR-139, BR-155): checkpoints and oracle O3 with the Engine session.
    add_executable(audio_qa_checkpoint_recovery_test checkpoint_recovery_test.cpp
        "${PROJECT_SOURCE_DIR}/src/audio_qa_inspection.cpp")
    target_include_directories(audio_qa_checkpoint_recovery_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src")
    target_link_libraries(audio_qa_checkpoint_recovery_test PRIVATE
        ayther_audio_qa_engine_replay ayther_replay_inspection Ayther::engine)
    target_compile_features(audio_qa_checkpoint_recovery_test PRIVATE cxx_std_20)
    set_target_properties(audio_qa_checkpoint_recovery_test PROPERTIES CXX_EXTENSIONS OFF)
    target_compile_options(audio_qa_checkpoint_recovery_test PRIVATE /W4 /WX /permissive- /EHsc)
    add_custom_command(TARGET audio_qa_checkpoint_recovery_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_RUNTIME_DLLS:audio_qa_checkpoint_recovery_test>"
            "$<TARGET_FILE_DIR:audio_qa_checkpoint_recovery_test>"
        COMMAND_EXPAND_LISTS VERBATIM)
    add_dependencies(audio_qa_checkpoint_recovery_test synthetic_libretro_core)
    add_test(NAME audio_qa_checkpoint_recovery
        COMMAND audio_qa_checkpoint_recovery_test
            "$<TARGET_FILE:synthetic_libretro_core>"
            "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/public-synthetic.aytest"
            "${CMAKE_CURRENT_BINARY_DIR}/fixtures/$<CONFIG>/public-synthetic-600.arp")
    set_tests_properties(audio_qa_checkpoint_recovery PROPERTIES
        LABELS "audio_qa;replay_inspection;integration" TIMEOUT 300
        FIXTURES_REQUIRED audio_qa_public_recording)
    # Spec 002 (BR-137 to BR-151): the inspection in the Runtime loop, no GPU.
    add_executable(audio_qa_runtime_inspection_session_test runtime_inspection_session_test.cpp)
    target_link_libraries(audio_qa_runtime_inspection_session_test PRIVATE ayther_audio_qa_cli
        ayther_audio_qa_transport ayther_audio_qa_model_toml)
    target_compile_features(audio_qa_runtime_inspection_session_test PRIVATE cxx_std_20)
    set_target_properties(audio_qa_runtime_inspection_session_test PROPERTIES
        CXX_EXTENSIONS OFF)
    target_compile_options(audio_qa_runtime_inspection_session_test PRIVATE
        /W4 /WX /permissive- /EHsc)
    add_custom_command(TARGET audio_qa_runtime_inspection_session_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${qa_descriptor_toml_dll_debug},${qa_descriptor_toml_dll_release}>"
            "$<TARGET_FILE_DIR:audio_qa_runtime_inspection_session_test>" VERBATIM)
    add_dependencies(audio_qa_runtime_inspection_session_test ayther_runtime
        synthetic_libretro_core audio_qa_public_recording_fixture)
    add_test(NAME audio_qa_runtime_inspection_session
        COMMAND audio_qa_runtime_inspection_session_test
            "$<TARGET_FILE:ayther_runtime>"
            "$<TARGET_FILE:synthetic_libretro_core>"
            "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/public-synthetic.aytest"
            "$<TARGET_FILE:audio_qa_public_recording_fixture>"
            "${CMAKE_CURRENT_BINARY_DIR}/runtime-inspection/$<CONFIG>")
    set_tests_properties(audio_qa_runtime_inspection_session PROPERTIES
        LABELS "audio_qa;runtime;replay_inspection;integration" TIMEOUT 300)
    # Spec 002 (BR-058): validation before admission with the real Runtime, no GPU.
    add_test(NAME audio_qa_check_preflight_integration
        COMMAND ${CMAKE_COMMAND}
            "-DCHECK_EXE=$<TARGET_FILE:ayther_audio_qa>"
            "-DRUNTIME_EXE=$<TARGET_FILE:ayther_runtime>"
            "-DCORE_DLL=$<TARGET_FILE:synthetic_libretro_core>"
            "-DNON_CORE_DLL=$<TARGET_FILE:non_libretro_library>"
            "-DROM_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/public-synthetic.aytest"
            "-DTAKE_FILE=${CMAKE_CURRENT_BINARY_DIR}/fixtures/$<CONFIG>/public-synthetic.arp"
            "-DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
            "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/check-preflight/$<CONFIG>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/check_preflight_integration_test.cmake")
    set_tests_properties(audio_qa_check_preflight_integration PROPERTIES
        LABELS "audio_qa;cli;runtime;integration"
        TIMEOUT 180 FIXTURES_REQUIRED audio_qa_public_recording
        ENVIRONMENT
            "SDL_AUDIO_DRIVER=dummy;SDL_AUDIO_FREQUENCY=48000;SDL_AUDIO_CHANNELS=2;SDL_AUDIO_DEVICE_SAMPLE_FRAMES=512")
    # Spec 002 (BR-056): the pack probe with a valid, an unverified and a corrupt pack.
    add_test(NAME audio_qa_runtime_pack_probe
        COMMAND ${CMAKE_COMMAND}
            "-DRUNTIME_EXE=$<TARGET_FILE:ayther_runtime>"
            "-DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/runtime_pack_probe_test.cmake")
    set_tests_properties(audio_qa_runtime_pack_probe PROPERTIES
        LABELS "audio_qa;runtime;contract;integration" TIMEOUT 60)
    # Spec 002 (BR-055): the core probe with the synthetic core and a non-libretro DLL.
    add_test(NAME audio_qa_runtime_probe_integration
        COMMAND audio_qa_runtime_probe_test
            "$<TARGET_FILE:ayther_runtime>"
            "$<TARGET_FILE:synthetic_libretro_core>"
            "$<TARGET_FILE:non_libretro_library>"
            "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/public-synthetic.aytest"
            "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
    set_tests_properties(audio_qa_runtime_probe_integration PROPERTIES
        LABELS "audio_qa;cli;runtime;integration" TIMEOUT 60)
    add_test(NAME audio_qa_real_replay_integration
        COMMAND ${CMAKE_COMMAND}
            "-DCHECK_EXE=$<TARGET_FILE:ayther_audio_qa>"
            "-DRUNTIME_EXE=$<TARGET_FILE:ayther_runtime>"
            "-DCORE_DLL=$<TARGET_FILE:synthetic_libretro_core>"
            "-DROM_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/public-synthetic.aytest"
            "-DPACK_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/public-synthetic.ay"
            "-DTRUST_REGISTRY=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/public-synthetic-trust.toml"
            "-DTAKE_FILE=${CMAKE_CURRENT_BINARY_DIR}/fixtures/$<CONFIG>/public-synthetic.arp"
            "-DBROKEN_TAKE_FILE=${CMAKE_CURRENT_BINARY_DIR}/fixtures/$<CONFIG>/public-synthetic-broken.arp"
            "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/real-replay/$<CONFIG>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/real_replay_integration_test.cmake")
    set_tests_properties(audio_qa_real_replay_integration PROPERTIES
        LABELS "audio_qa;cli;runtime;engine;integration"
        TIMEOUT 45 FIXTURES_REQUIRED audio_qa_public_recording
        ENVIRONMENT
            "SDL_AUDIO_DRIVER=dummy;SDL_AUDIO_FREQUENCY=48000;SDL_AUDIO_CHANNELS=2;SDL_AUDIO_DEVICE_SAMPLE_FRAMES=512")

    if(AYTHER_ENABLE_GPU_TESTS)
        add_test(NAME audio_qa_visible_replay_integration
            COMMAND ${CMAKE_COMMAND}
                "-DCHECK_EXE=$<TARGET_FILE:ayther_audio_qa>"
                "-DRUNTIME_EXE=$<TARGET_FILE:ayther_runtime>"
                "-DCORE_DLL=$<TARGET_FILE:synthetic_libretro_core>"
                "-DROM_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/public-synthetic.aytest"
                "-DPACK_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/public-synthetic.ay"
                "-DTRUST_REGISTRY=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/public-synthetic-trust.toml"
                "-DTAKE_FILE=${CMAKE_CURRENT_BINARY_DIR}/fixtures/$<CONFIG>/public-synthetic.arp"
                "-DBROKEN_TAKE_FILE=${CMAKE_CURRENT_BINARY_DIR}/fixtures/$<CONFIG>/public-synthetic-broken.arp"
                "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/visible-replay/$<CONFIG>"
                "-DFIXTURE_EXE=$<TARGET_FILE:audio_qa_public_recording_fixture>"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/visible_replay_integration_test.cmake")
        set_tests_properties(audio_qa_visible_replay_integration PROPERTIES
            LABELS "audio_qa;gpu;integration" TIMEOUT 90 RUN_SERIAL TRUE
            FIXTURES_REQUIRED audio_qa_public_recording)
        # Spec 002 (BR-144 to BR-148, BR-153, BR-154): the visible inspection with simulated
        # events, through a synthetic supervisor.
        add_executable(audio_qa_visible_inspection_test runtime_visible_inspection_test.cpp)
        target_link_libraries(audio_qa_visible_inspection_test PRIVATE ayther_audio_qa_cli
            ayther_audio_qa_transport ayther_audio_qa_model_toml)
        target_compile_features(audio_qa_visible_inspection_test PRIVATE cxx_std_20)
        set_target_properties(audio_qa_visible_inspection_test PROPERTIES CXX_EXTENSIONS OFF)
        target_compile_options(audio_qa_visible_inspection_test PRIVATE
            /W4 /WX /permissive- /EHsc)
        add_custom_command(TARGET audio_qa_visible_inspection_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<IF:$<CONFIG:Debug>,${qa_descriptor_toml_dll_debug},${qa_descriptor_toml_dll_release}>"
                "$<TARGET_FILE_DIR:audio_qa_visible_inspection_test>" VERBATIM)
        add_dependencies(audio_qa_visible_inspection_test ayther_runtime synthetic_libretro_core
            audio_qa_public_recording_fixture)
        add_test(NAME audio_qa_visible_inspection_integration
            COMMAND audio_qa_visible_inspection_test
                "$<TARGET_FILE:ayther_runtime>"
                "$<TARGET_FILE:synthetic_libretro_core>"
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/public-synthetic.aytest"
                "$<TARGET_FILE:audio_qa_public_recording_fixture>"
                "${CMAKE_CURRENT_BINARY_DIR}/visible-inspection/$<CONFIG>")
        set_tests_properties(audio_qa_visible_inspection_integration PROPERTIES
            LABELS "audio_qa;gpu;integration;replay_inspection" TIMEOUT 300 RUN_SERIAL TRUE)
        # Spec 002 (BR-156): the measurements of plan §8 on the reference equipment. Not a
        # ctest: it is run by hand on an idle machine and its table goes to evidence/perf.md.
        add_executable(audio_qa_inspection_measurement inspection_measurement.cpp)
        target_link_libraries(audio_qa_inspection_measurement PRIVATE ayther_audio_qa_cli
            ayther_audio_qa_transport ayther_audio_qa_model_toml ayther_replay_inspection)
        target_compile_features(audio_qa_inspection_measurement PRIVATE cxx_std_20)
        set_target_properties(audio_qa_inspection_measurement PROPERTIES CXX_EXTENSIONS OFF)
        target_compile_options(audio_qa_inspection_measurement PRIVATE /W4 /WX /permissive- /EHsc)
        add_custom_command(TARGET audio_qa_inspection_measurement POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<IF:$<CONFIG:Debug>,${qa_descriptor_toml_dll_debug},${qa_descriptor_toml_dll_release}>"
                "$<TARGET_FILE_DIR:audio_qa_inspection_measurement>" VERBATIM)
        # BR-156 (DI-11): the measurement drives a real take (Toma 3) with its pack, zones and
        # rate; the dry run checks the plan without timing anything.
        add_test(NAME audio_qa_inspection_measurement_plan
            COMMAND audio_qa_inspection_measurement
                "$<TARGET_FILE:ayther_runtime>"
                "$<TARGET_FILE:synthetic_libretro_core>"
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/public-synthetic.aytest"
                "$<TARGET_FILE:audio_qa_public_recording_fixture>"
                "${CMAKE_CURRENT_BINARY_DIR}/inspection-measurement-plan/$<CONFIG>"
                --take "${CMAKE_CURRENT_BINARY_DIR}/fixtures/$<CONFIG>/public-synthetic-600.arp"
                --zones 100,300,500 --fps 59.922743 --core-option region=us
                --pack "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/public-synthetic.ay"
                --trust-registry "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/public-synthetic-trust.toml"
                --dry-run)
        set_tests_properties(audio_qa_inspection_measurement_plan PROPERTIES
            LABELS "audio_qa;perf;unit" TIMEOUT 30 FIXTURES_REQUIRED audio_qa_public_recording
            PASS_REGULAR_EXPRESSION
                "plan take_frames=600 zones=100,300,500 fps=59.9227 pack=yes trust=yes core_options=1")
    endif()

    set(AYTHER_AUDIO_QA_LEGACY_RUNTIME "" CACHE FILEPATH
        "Historical Runtime executable used by the QA compatibility test")
    if(AYTHER_AUDIO_QA_LEGACY_RUNTIME AND
       EXISTS "${AYTHER_AUDIO_QA_LEGACY_RUNTIME}")
        add_test(NAME audio_qa_legacy_runtime_cli
            COMMAND ${CMAKE_COMMAND}
                "-DCHECK_EXE=$<TARGET_FILE:ayther_audio_qa>"
                "-DLEGACY_RUNTIME_EXE=${AYTHER_AUDIO_QA_LEGACY_RUNTIME}"
                "-DCORE_DLL=$<TARGET_FILE:synthetic_libretro_core>"
                "-DROM_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/public-synthetic.aytest"
                "-DPACK_FILE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/public-synthetic.ay"
                "-DTRUST_REGISTRY=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/public-synthetic-trust.toml"
                "-DTAKE_FILE=${CMAKE_CURRENT_BINARY_DIR}/fixtures/$<CONFIG>/public-synthetic.arp"
                "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/legacy-runtime/$<CONFIG>"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/legacy_runtime_cli_test.cmake")
        set_tests_properties(audio_qa_legacy_runtime_cli PROPERTIES
            LABELS "audio_qa;cli;runtime;compatibility;integration"
            TIMEOUT 20 FIXTURES_REQUIRED audio_qa_public_recording)
    endif()
endif()
add_executable(audio_qa_model_test model_test.cpp)
target_link_libraries(audio_qa_model_test PRIVATE ayther_audio_qa_model_toml)
set_target_properties(audio_qa_model_test PROPERTIES CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_model_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_model_test PRIVATE /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_model_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
if(WIN32)
    # The pinned package exports an UNKNOWN library target, so runtime DLLs are explicit.
    find_file(qa_toml_dll_release NAMES tomlplusplus-3.dll
        PATHS "${tomlplusplus_DIR}/../../bin" NO_DEFAULT_PATH REQUIRED)
    find_file(qa_toml_dll_debug NAMES tomlplusplus-3.dll
        PATHS "${tomlplusplus_DIR}/../../debug/bin" NO_DEFAULT_PATH REQUIRED)
    add_custom_command(TARGET audio_qa_model_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${qa_toml_dll_debug},${qa_toml_dll_release}>"
            "$<TARGET_FILE_DIR:audio_qa_model_test>" VERBATIM)
endif()
foreach(mode IN ITEMS request run reject)
    add_test(NAME audio_qa_model_${mode} COMMAND audio_qa_model_test "${mode}")
    set_tests_properties(audio_qa_model_${mode} PROPERTIES LABELS "audio_qa;contract" TIMEOUT 10)
endforeach()

set(audio_qa_required_ctest_families
    "audio_qa,unit,contract,integration,limits,protocol,fixture")
add_test(NAME audio_qa_required_ctest_families
    COMMAND ${CMAKE_COMMAND}
        "-DQA_BUILD_DIRECTORY=${CMAKE_BINARY_DIR}"
        "-DQA_CTEST_COMMAND=${CMAKE_CTEST_COMMAND}"
        "-DQA_CONFIGURATION=$<CONFIG>"
        "-DQA_REQUIRED_FAMILIES=${audio_qa_required_ctest_families}"
        -P "${CMAKE_CURRENT_LIST_DIR}/../../tools/check_ctest_families.cmake")
set_tests_properties(audio_qa_required_ctest_families PROPERTIES
    LABELS "audio_qa;contract" TIMEOUT 30)

add_test(NAME audio_qa_required_ctest_family_empty
    COMMAND ${CMAKE_COMMAND}
        "-DQA_BUILD_DIRECTORY=${CMAKE_BINARY_DIR}"
        "-DQA_CTEST_COMMAND=${CMAKE_CTEST_COMMAND}"
        "-DQA_CONFIGURATION=$<CONFIG>"
        -DQA_REQUIRED_FAMILIES=audio_qa_family_that_must_not_exist
        -P "${CMAKE_CURRENT_LIST_DIR}/../../tools/check_ctest_families.cmake")
set_tests_properties(audio_qa_required_ctest_family_empty PROPERTIES
    LABELS "audio_qa;contract" TIMEOUT 30 WILL_FAIL TRUE)

foreach(model IN ITEMS reference fact mix audio initial_state coverage capabilities)
    add_executable(audio_qa_${model}_model_test ${model}_model_test.cpp)
    target_link_libraries(audio_qa_${model}_model_test PRIVATE ayther_audio_qa_domain)
    set_target_properties(audio_qa_${model}_model_test PROPERTIES CXX_EXTENSIONS OFF)
    if(AYTHER_CLANG_TIDY_COMMAND)
        set_target_properties(audio_qa_${model}_model_test PROPERTIES
            CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
    endif()
    if(MSVC)
        target_compile_options(audio_qa_${model}_model_test PRIVATE /W4 /WX /permissive- /EHsc)
    else()
        target_compile_options(audio_qa_${model}_model_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME audio_qa_${model}_model COMMAND audio_qa_${model}_model_test)
    set_tests_properties(audio_qa_${model}_model PROPERTIES LABELS "audio_qa;contract" TIMEOUT 10)
endforeach()

add_executable(audio_qa_assignment_inventory_summary_test
    assignment_inventory_summary_test.cpp)
target_link_libraries(audio_qa_assignment_inventory_summary_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_assignment_inventory_summary_test PRIVATE
    cxx_std_20)
set_target_properties(audio_qa_assignment_inventory_summary_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_assignment_inventory_summary_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_assignment_inventory_summary_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_assignment_inventory_summary_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_assignment_inventory_summary
    COMMAND audio_qa_assignment_inventory_summary_test)
set_tests_properties(audio_qa_assignment_inventory_summary PROPERTIES
    LABELS "audio_qa;unit" TIMEOUT 10)

add_executable(audio_qa_assignment_stage_coverage_test
    assignment_stage_coverage_test.cpp)
target_link_libraries(audio_qa_assignment_stage_coverage_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_assignment_stage_coverage_test PRIVATE
    cxx_std_20)
set_target_properties(audio_qa_assignment_stage_coverage_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_assignment_stage_coverage_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_assignment_stage_coverage_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_assignment_stage_coverage_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_assignment_stage_coverage
    COMMAND audio_qa_assignment_stage_coverage_test)
set_tests_properties(audio_qa_assignment_stage_coverage PROPERTIES
    LABELS "audio_qa;unit" TIMEOUT 10)

add_executable(audio_qa_assignment_pending_report_test
    assignment_pending_report_test.cpp)
target_link_libraries(audio_qa_assignment_pending_report_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_assignment_pending_report_test PRIVATE
    cxx_std_20)
set_target_properties(audio_qa_assignment_pending_report_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_assignment_pending_report_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_assignment_pending_report_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_assignment_pending_report_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_assignment_pending_report
    COMMAND audio_qa_assignment_pending_report_test)
set_tests_properties(audio_qa_assignment_pending_report PROPERTIES
    LABELS "audio_qa;unit" TIMEOUT 10)

add_executable(audio_qa_assignment_coverage_aggregation_test
    assignment_coverage_aggregation_test.cpp)
target_link_libraries(audio_qa_assignment_coverage_aggregation_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_assignment_coverage_aggregation_test PRIVATE
    cxx_std_20)
set_target_properties(audio_qa_assignment_coverage_aggregation_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_assignment_coverage_aggregation_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_assignment_coverage_aggregation_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_assignment_coverage_aggregation_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_assignment_coverage_aggregation
    COMMAND audio_qa_assignment_coverage_aggregation_test)
set_tests_properties(audio_qa_assignment_coverage_aggregation PROPERTIES
    LABELS "audio_qa;unit" TIMEOUT 10)

add_executable(audio_qa_trace_query_test trace_query_test.cpp)
target_link_libraries(audio_qa_trace_query_test PRIVATE ayther_audio_qa_domain)
target_compile_features(audio_qa_trace_query_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_trace_query_test PROPERTIES CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_trace_query_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_trace_query_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_trace_query_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_trace_query COMMAND audio_qa_trace_query_test)
set_tests_properties(audio_qa_trace_query PROPERTIES
    LABELS "audio_qa;unit" TIMEOUT 10)

add_executable(audio_qa_coexistence_query_test coexistence_query_test.cpp)
target_link_libraries(audio_qa_coexistence_query_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_coexistence_query_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_coexistence_query_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_coexistence_query_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_coexistence_query_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_coexistence_query_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_coexistence_query
    COMMAND audio_qa_coexistence_query_test)
set_tests_properties(audio_qa_coexistence_query PROPERTIES
    LABELS "audio_qa;unit" TIMEOUT 10)

add_executable(audio_qa_runtime_reference_comparison_test
    runtime_reference_comparison_test.cpp)
target_link_libraries(audio_qa_runtime_reference_comparison_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_runtime_reference_comparison_test PRIVATE
    cxx_std_20)
set_target_properties(audio_qa_runtime_reference_comparison_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_runtime_reference_comparison_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_runtime_reference_comparison_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_runtime_reference_comparison_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_runtime_reference_comparison
    COMMAND audio_qa_runtime_reference_comparison_test)
set_tests_properties(audio_qa_runtime_reference_comparison PROPERTIES
    LABELS "audio_qa;contract" TIMEOUT 10)

add_executable(audio_qa_pinned_take_test pinned_take_test.cpp)
target_link_libraries(audio_qa_pinned_take_test PRIVATE
    ayther_audio_qa_materials)
target_compile_features(audio_qa_pinned_take_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_pinned_take_test PROPERTIES CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_pinned_take_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_pinned_take_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_pinned_take_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_pinned_take COMMAND audio_qa_pinned_take_test)
set_tests_properties(audio_qa_pinned_take PROPERTIES
    LABELS "audio_qa;unit" TIMEOUT 10)

add_executable(audio_qa_pinned_pack_test pinned_pack_test.cpp)
target_link_libraries(audio_qa_pinned_pack_test PRIVATE
    ayther_audio_qa_materials)
target_compile_features(audio_qa_pinned_pack_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_pinned_pack_test PROPERTIES CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_pinned_pack_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_pinned_pack_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_pinned_pack_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_pinned_pack COMMAND audio_qa_pinned_pack_test)
set_tests_properties(audio_qa_pinned_pack PROPERTIES
    LABELS "audio_qa;unit" TIMEOUT 10)

add_executable(audio_qa_pinned_game_materials_test
    pinned_game_materials_test.cpp)
target_link_libraries(audio_qa_pinned_game_materials_test PRIVATE
    ayther_audio_qa_materials)
target_compile_features(audio_qa_pinned_game_materials_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_pinned_game_materials_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_pinned_game_materials_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_pinned_game_materials_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_pinned_game_materials_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_pinned_game_materials
    COMMAND audio_qa_pinned_game_materials_test)
set_tests_properties(audio_qa_pinned_game_materials PROPERTIES
    LABELS "audio_qa;unit" TIMEOUT 10)

add_executable(audio_qa_material_staging_budget_test
    material_staging_budget_test.cpp)
target_link_libraries(audio_qa_material_staging_budget_test PRIVATE
    ayther_audio_qa_materials)
target_compile_features(audio_qa_material_staging_budget_test PRIVATE
    cxx_std_20)
set_target_properties(audio_qa_material_staging_budget_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_material_staging_budget_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_material_staging_budget_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_material_staging_budget_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_material_staging_budget
    COMMAND audio_qa_material_staging_budget_test)
set_tests_properties(audio_qa_material_staging_budget PROPERTIES
    LABELS "audio_qa;integration;limits" TIMEOUT 10)

add_executable(audio_qa_isolated_saves_test
    isolated_saves_test.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/runtime_config.cpp")
target_include_directories(audio_qa_isolated_saves_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
target_link_libraries(audio_qa_isolated_saves_test PRIVATE
    ayther_audio_qa_preparation)
target_compile_features(audio_qa_isolated_saves_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_isolated_saves_test PROPERTIES CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_isolated_saves_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_isolated_saves_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_isolated_saves_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_isolated_saves COMMAND audio_qa_isolated_saves_test)
set_tests_properties(audio_qa_isolated_saves PROPERTIES
    LABELS "audio_qa;integration" TIMEOUT 10)

add_executable(audio_qa_ordinary_save_resume_test
    ordinary_save_resume_test.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/save_state_store.cpp")
target_include_directories(audio_qa_ordinary_save_resume_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
target_link_libraries(audio_qa_ordinary_save_resume_test PRIVATE
    ayther_audio_qa_preparation)
target_compile_features(audio_qa_ordinary_save_resume_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_ordinary_save_resume_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_ordinary_save_resume_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_ordinary_save_resume_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_ordinary_save_resume_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_ordinary_save_resume
    COMMAND audio_qa_ordinary_save_resume_test)
set_tests_properties(audio_qa_ordinary_save_resume PROPERTIES
    LABELS "audio_qa;integration" TIMEOUT 10)

add_executable(audio_qa_isolated_runtime_data_test
    isolated_runtime_data_test.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/runtime_config.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/player_config.cpp")
target_include_directories(audio_qa_isolated_runtime_data_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
target_link_libraries(audio_qa_isolated_runtime_data_test PRIVATE
    ayther_audio_qa_preparation)
target_compile_features(audio_qa_isolated_runtime_data_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_isolated_runtime_data_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_isolated_runtime_data_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_isolated_runtime_data_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_isolated_runtime_data_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_isolated_runtime_data
    COMMAND audio_qa_isolated_runtime_data_test)
set_tests_properties(audio_qa_isolated_runtime_data PROPERTIES
    LABELS "audio_qa;integration" TIMEOUT 10)

add_executable(audio_qa_material_consumption_guard_test
    material_consumption_guard_test.cpp)
target_link_libraries(audio_qa_material_consumption_guard_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_material_consumption_guard_test PRIVATE
    cxx_std_20)
set_target_properties(audio_qa_material_consumption_guard_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_material_consumption_guard_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_material_consumption_guard_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_material_consumption_guard_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_material_consumption_guard
    COMMAND audio_qa_material_consumption_guard_test)
set_tests_properties(audio_qa_material_consumption_guard PROPERTIES
    LABELS "audio_qa;unit" TIMEOUT 10)

add_executable(audio_qa_exclusive_evidence_directory_test
    exclusive_evidence_directory_test.cpp)
target_link_libraries(audio_qa_exclusive_evidence_directory_test PRIVATE
    ayther_audio_qa_evidence)
target_compile_features(audio_qa_exclusive_evidence_directory_test PRIVATE
    cxx_std_20)
set_target_properties(audio_qa_exclusive_evidence_directory_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_exclusive_evidence_directory_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_exclusive_evidence_directory_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_exclusive_evidence_directory_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_exclusive_evidence_directory
    COMMAND audio_qa_exclusive_evidence_directory_test)
set_tests_properties(audio_qa_exclusive_evidence_directory PROPERTIES
    LABELS "audio_qa;integration" TIMEOUT 10)

add_executable(audio_qa_reference_store_test reference_store_test.cpp)
target_link_libraries(audio_qa_reference_store_test PRIVATE
    ayther_audio_qa_evidence)
target_compile_features(audio_qa_reference_store_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_reference_store_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_reference_store_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_reference_store_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_reference_store_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
if(WIN32)
    add_custom_command(TARGET audio_qa_reference_store_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${qa_toml_dll_debug},${qa_toml_dll_release}>"
            "$<TARGET_FILE_DIR:audio_qa_reference_store_test>" VERBATIM)
endif()
add_test(NAME audio_qa_reference_store COMMAND audio_qa_reference_store_test)
set_tests_properties(audio_qa_reference_store PROPERTIES
    LABELS "audio_qa;integration" TIMEOUT 10)

add_executable(audio_qa_fact_fragment_store_test
    fact_fragment_store_test.cpp)
target_link_libraries(audio_qa_fact_fragment_store_test PRIVATE
    ayther_audio_qa_evidence)
target_compile_features(audio_qa_fact_fragment_store_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_fact_fragment_store_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_fact_fragment_store_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_fact_fragment_store_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_fact_fragment_store_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
if(WIN32)
    add_custom_command(TARGET audio_qa_fact_fragment_store_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${qa_toml_dll_debug},${qa_toml_dll_release}>"
            "$<TARGET_FILE_DIR:audio_qa_fact_fragment_store_test>" VERBATIM)
endif()
add_test(NAME audio_qa_fact_fragment_store
    COMMAND audio_qa_fact_fragment_store_test)
set_tests_properties(audio_qa_fact_fragment_store PROPERTIES
    LABELS "audio_qa;integration" TIMEOUT 10)

add_executable(audio_qa_fact_integrity_test fact_integrity_test.cpp)
target_link_libraries(audio_qa_fact_integrity_test PRIVATE
    ayther_audio_qa_evidence)
target_compile_features(audio_qa_fact_integrity_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_fact_integrity_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_fact_integrity_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_fact_integrity_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_fact_integrity_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
if(WIN32)
    add_custom_command(TARGET audio_qa_fact_integrity_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${qa_toml_dll_debug},${qa_toml_dll_release}>"
            "$<TARGET_FILE_DIR:audio_qa_fact_integrity_test>" VERBATIM)
endif()
add_test(NAME audio_qa_fact_integrity COMMAND audio_qa_fact_integrity_test)
set_tests_properties(audio_qa_fact_integrity PROPERTIES
    LABELS "audio_qa;integration;integrity" TIMEOUT 10)

add_executable(audio_qa_fact_relationship_test fact_relationship_test.cpp)
target_link_libraries(audio_qa_fact_relationship_test PRIVATE
    ayther_audio_qa_evidence)
target_compile_features(audio_qa_fact_relationship_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_fact_relationship_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_fact_relationship_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_fact_relationship_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_fact_relationship_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
if(WIN32)
    add_custom_command(TARGET audio_qa_fact_relationship_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${qa_toml_dll_debug},${qa_toml_dll_release}>"
            "$<TARGET_FILE_DIR:audio_qa_fact_relationship_test>" VERBATIM)
endif()
add_test(NAME audio_qa_fact_relationship COMMAND audio_qa_fact_relationship_test)
set_tests_properties(audio_qa_fact_relationship PROPERTIES
    LABELS "audio_qa;integration;integrity" TIMEOUT 10)

add_executable(audio_qa_fact_shared_order_test fact_shared_order_test.cpp)
target_link_libraries(audio_qa_fact_shared_order_test PRIVATE
    ayther_audio_qa_evidence)
target_compile_features(audio_qa_fact_shared_order_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_fact_shared_order_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_fact_shared_order_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_fact_shared_order_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_fact_shared_order_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
if(WIN32)
    add_custom_command(TARGET audio_qa_fact_shared_order_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${qa_toml_dll_debug},${qa_toml_dll_release}>"
            "$<TARGET_FILE_DIR:audio_qa_fact_shared_order_test>" VERBATIM)
endif()
add_test(NAME audio_qa_fact_shared_order COMMAND audio_qa_fact_shared_order_test)
set_tests_properties(audio_qa_fact_shared_order PROPERTIES
    LABELS "audio_qa;integration;integrity" TIMEOUT 10)

add_executable(audio_qa_pcm_block_store_test pcm_block_store_test.cpp)
target_link_libraries(audio_qa_pcm_block_store_test PRIVATE
    ayther_audio_qa_evidence)
target_compile_features(audio_qa_pcm_block_store_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_pcm_block_store_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_pcm_block_store_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_pcm_block_store_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_pcm_block_store_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
if(WIN32)
    add_custom_command(TARGET audio_qa_pcm_block_store_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${qa_toml_dll_debug},${qa_toml_dll_release}>"
            "$<TARGET_FILE_DIR:audio_qa_pcm_block_store_test>" VERBATIM)
endif()
add_test(NAME audio_qa_pcm_block_store COMMAND audio_qa_pcm_block_store_test)
set_tests_properties(audio_qa_pcm_block_store PROPERTIES
    LABELS "audio_qa;integration" TIMEOUT 10)

add_executable(audio_qa_audio_integrity_test audio_integrity_test.cpp)
target_link_libraries(audio_qa_audio_integrity_test PRIVATE
    ayther_audio_qa_evidence)
target_compile_features(audio_qa_audio_integrity_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_audio_integrity_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_audio_integrity_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_audio_integrity_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_audio_integrity_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
if(WIN32)
    add_custom_command(TARGET audio_qa_audio_integrity_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${qa_toml_dll_debug},${qa_toml_dll_release}>"
            "$<TARGET_FILE_DIR:audio_qa_audio_integrity_test>" VERBATIM)
endif()
add_test(NAME audio_qa_audio_integrity COMMAND audio_qa_audio_integrity_test)
set_tests_properties(audio_qa_audio_integrity PROPERTIES
    LABELS "audio_qa;integration;integrity" TIMEOUT 10)

add_executable(audio_qa_durable_file_test durable_file_test.cpp)
target_link_libraries(audio_qa_durable_file_test PRIVATE
    ayther_audio_qa_evidence)
target_compile_features(audio_qa_durable_file_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_durable_file_test PROPERTIES CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_durable_file_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_durable_file_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_durable_file_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
if(WIN32)
    add_custom_command(TARGET audio_qa_durable_file_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${qa_toml_dll_debug},${qa_toml_dll_release}>"
            "$<TARGET_FILE_DIR:audio_qa_durable_file_test>" VERBATIM)
endif()
add_test(NAME audio_qa_durable_file COMMAND audio_qa_durable_file_test)
set_tests_properties(audio_qa_durable_file PROPERTIES
    LABELS "audio_qa;integration" TIMEOUT 10)

add_executable(audio_qa_checkpoint_store_test checkpoint_store_test.cpp)
target_link_libraries(audio_qa_checkpoint_store_test PRIVATE
    ayther_audio_qa_evidence)
target_compile_features(audio_qa_checkpoint_store_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_checkpoint_store_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_checkpoint_store_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_checkpoint_store_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_checkpoint_store_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
if(WIN32)
    add_custom_command(TARGET audio_qa_checkpoint_store_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${qa_toml_dll_debug},${qa_toml_dll_release}>"
            "$<TARGET_FILE_DIR:audio_qa_checkpoint_store_test>" VERBATIM)
endif()
add_test(NAME audio_qa_checkpoint_store COMMAND audio_qa_checkpoint_store_test)
set_tests_properties(audio_qa_checkpoint_store PROPERTIES
    LABELS "audio_qa;integration" TIMEOUT 10)

add_executable(audio_qa_checkpoint_threshold_test checkpoint_threshold_test.cpp)
target_link_libraries(audio_qa_checkpoint_threshold_test PRIVATE
    ayther_audio_qa_evidence)
target_compile_features(audio_qa_checkpoint_threshold_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_checkpoint_threshold_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_checkpoint_threshold_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_checkpoint_threshold_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_checkpoint_threshold_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
if(WIN32)
    add_custom_command(TARGET audio_qa_checkpoint_threshold_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${qa_toml_dll_debug},${qa_toml_dll_release}>"
            "$<TARGET_FILE_DIR:audio_qa_checkpoint_threshold_test>" VERBATIM)
endif()
add_test(NAME audio_qa_checkpoint_threshold
    COMMAND audio_qa_checkpoint_threshold_test)
set_tests_properties(audio_qa_checkpoint_threshold PROPERTIES
    LABELS "audio_qa;integration" TIMEOUT 10)

add_executable(audio_qa_request_ledger_test request_ledger_test.cpp)
target_link_libraries(audio_qa_request_ledger_test PRIVATE
    ayther_audio_qa_evidence)
target_compile_features(audio_qa_request_ledger_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_request_ledger_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_request_ledger_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_request_ledger_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_request_ledger_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
if(WIN32)
    add_custom_command(TARGET audio_qa_request_ledger_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${qa_toml_dll_debug},${qa_toml_dll_release}>"
            "$<TARGET_FILE_DIR:audio_qa_request_ledger_test>" VERBATIM)
endif()
add_test(NAME audio_qa_request_ledger COMMAND audio_qa_request_ledger_test)
set_tests_properties(audio_qa_request_ledger PROPERTIES
    LABELS "audio_qa;integration" TIMEOUT 10)

add_executable(audio_qa_run_recovery_test run_recovery_test.cpp)
target_link_libraries(audio_qa_run_recovery_test PRIVATE
    ayther_audio_qa_evidence)
target_compile_features(audio_qa_run_recovery_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_run_recovery_test PROPERTIES CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_run_recovery_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_run_recovery_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_run_recovery_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
if(WIN32)
    add_custom_command(TARGET audio_qa_run_recovery_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${qa_toml_dll_debug},${qa_toml_dll_release}>"
            "$<TARGET_FILE_DIR:audio_qa_run_recovery_test>" VERBATIM)
endif()
add_test(NAME audio_qa_run_recovery COMMAND audio_qa_run_recovery_test)
set_tests_properties(audio_qa_run_recovery PROPERTIES
    LABELS "audio_qa;integration" TIMEOUT 10)

add_executable(audio_qa_checkpoint_artifact_audit_test
    checkpoint_artifact_audit_test.cpp)
target_link_libraries(audio_qa_checkpoint_artifact_audit_test PRIVATE
    ayther_audio_qa_evidence)
target_compile_features(audio_qa_checkpoint_artifact_audit_test PRIVATE
    cxx_std_20)
set_target_properties(audio_qa_checkpoint_artifact_audit_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_checkpoint_artifact_audit_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_checkpoint_artifact_audit_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_checkpoint_artifact_audit_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
if(WIN32)
    add_custom_command(TARGET audio_qa_checkpoint_artifact_audit_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${qa_toml_dll_debug},${qa_toml_dll_release}>"
            "$<TARGET_FILE_DIR:audio_qa_checkpoint_artifact_audit_test>"
        VERBATIM)
endif()
add_test(NAME audio_qa_checkpoint_artifact_audit
    COMMAND audio_qa_checkpoint_artifact_audit_test)
set_tests_properties(audio_qa_checkpoint_artifact_audit PROPERTIES
    LABELS "audio_qa;integration" TIMEOUT 10)

add_executable(audio_qa_evidence_schema_compatibility_test
    evidence_schema_compatibility_test.cpp)
target_link_libraries(audio_qa_evidence_schema_compatibility_test PRIVATE
    ayther_audio_qa_evidence)
target_compile_features(audio_qa_evidence_schema_compatibility_test PRIVATE
    cxx_std_20)
set_target_properties(audio_qa_evidence_schema_compatibility_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_evidence_schema_compatibility_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_evidence_schema_compatibility_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_evidence_schema_compatibility_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
if(WIN32)
    add_custom_command(TARGET audio_qa_evidence_schema_compatibility_test
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${qa_toml_dll_debug},${qa_toml_dll_release}>"
            "$<TARGET_FILE_DIR:audio_qa_evidence_schema_compatibility_test>"
        VERBATIM)
endif()
add_test(NAME audio_qa_evidence_schema_compatibility
    COMMAND audio_qa_evidence_schema_compatibility_test)
set_tests_properties(audio_qa_evidence_schema_compatibility PROPERTIES
    LABELS "audio_qa;integration;contract" TIMEOUT 10)

add_executable(audio_qa_wav_derivation_test wav_derivation_test.cpp)
target_link_libraries(audio_qa_wav_derivation_test PRIVATE
    ayther_audio_qa_evidence)
target_compile_features(audio_qa_wav_derivation_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_wav_derivation_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_wav_derivation_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_wav_derivation_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_wav_derivation_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
if(WIN32)
    add_custom_command(TARGET audio_qa_wav_derivation_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${qa_toml_dll_debug},${qa_toml_dll_release}>"
            "$<TARGET_FILE_DIR:audio_qa_wav_derivation_test>" VERBATIM)
endif()
add_test(NAME audio_qa_wav_derivation COMMAND audio_qa_wav_derivation_test)
set_tests_properties(audio_qa_wav_derivation PROPERTIES
    LABELS "audio_qa;integration" TIMEOUT 10)

add_executable(audio_qa_disk_full_publication_test
    disk_full_publication_test.cpp)
target_link_libraries(audio_qa_disk_full_publication_test PRIVATE
    ayther_audio_qa_evidence)
target_compile_features(audio_qa_disk_full_publication_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_disk_full_publication_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_disk_full_publication_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_disk_full_publication_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_disk_full_publication_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
if(WIN32)
    add_custom_command(TARGET audio_qa_disk_full_publication_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${qa_toml_dll_debug},${qa_toml_dll_release}>"
            "$<TARGET_FILE_DIR:audio_qa_disk_full_publication_test>" VERBATIM)
endif()
add_test(NAME audio_qa_disk_full_publication
    COMMAND audio_qa_disk_full_publication_test)
set_tests_properties(audio_qa_disk_full_publication PROPERTIES
    LABELS "audio_qa;integration;fault" TIMEOUT 10)

add_executable(audio_qa_durable_boundary_test durable_boundary_test.cpp)
target_link_libraries(audio_qa_durable_boundary_test PRIVATE
    ayther_audio_qa_evidence)
target_compile_features(audio_qa_durable_boundary_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_durable_boundary_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_durable_boundary_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_durable_boundary_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_durable_boundary_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
if(WIN32)
    add_custom_command(TARGET audio_qa_durable_boundary_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${qa_toml_dll_debug},${qa_toml_dll_release}>"
            "$<TARGET_FILE_DIR:audio_qa_durable_boundary_test>" VERBATIM)
endif()
add_test(NAME audio_qa_durable_boundary COMMAND audio_qa_durable_boundary_test)
set_tests_properties(audio_qa_durable_boundary PROPERTIES
    LABELS "audio_qa;integration;fault" TIMEOUT 10)

add_executable(audio_qa_play_launch_manifest_test
    play_launch_manifest_test.cpp)
target_link_libraries(audio_qa_play_launch_manifest_test PRIVATE
    ayther_audio_qa_model_toml)
target_compile_features(audio_qa_play_launch_manifest_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_play_launch_manifest_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_play_launch_manifest_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_play_launch_manifest_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_play_launch_manifest_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
if(WIN32)
    add_custom_command(TARGET audio_qa_play_launch_manifest_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${qa_toml_dll_debug},${qa_toml_dll_release}>"
            "$<TARGET_FILE_DIR:audio_qa_play_launch_manifest_test>" VERBATIM)
endif()
add_test(NAME audio_qa_play_launch_manifest
    COMMAND audio_qa_play_launch_manifest_test)
set_tests_properties(audio_qa_play_launch_manifest PROPERTIES
    LABELS "audio_qa;unit" TIMEOUT 10)

add_executable(audio_qa_protocol_header_test protocol_header_test.cpp)
target_link_libraries(audio_qa_protocol_header_test PRIVATE ayther_audio_qa_domain)
target_compile_features(audio_qa_protocol_header_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_protocol_header_test PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(audio_qa_protocol_header_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_protocol_header_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_protocol_header COMMAND audio_qa_protocol_header_test)
set_tests_properties(audio_qa_protocol_header PROPERTIES
    LABELS "audio_qa;contract" TIMEOUT 10)

add_executable(audio_qa_recording_header_test recording_header_test.cpp)
target_link_libraries(audio_qa_recording_header_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_recording_header_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_recording_header_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_recording_header_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_recording_header_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_recording_header_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_recording_header COMMAND audio_qa_recording_header_test)
set_tests_properties(audio_qa_recording_header PROPERTIES
    LABELS "audio_qa;contract" TIMEOUT 10)

add_executable(audio_qa_recording_layout_test recording_layout_test.cpp)
target_link_libraries(audio_qa_recording_layout_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_recording_layout_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_recording_layout_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_recording_layout_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_recording_layout_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_recording_layout_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_recording_layout COMMAND audio_qa_recording_layout_test)
set_tests_properties(audio_qa_recording_layout PROPERTIES
    LABELS "audio_qa;contract" TIMEOUT 10)

add_executable(audio_qa_recording_input_source_test
    recording_input_source_test.cpp)
target_link_libraries(audio_qa_recording_input_source_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_recording_input_source_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_recording_input_source_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_recording_input_source_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_recording_input_source_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_recording_input_source_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_recording_input_source
    COMMAND audio_qa_recording_input_source_test)
set_tests_properties(audio_qa_recording_input_source PROPERTIES
    LABELS "audio_qa;contract" TIMEOUT 10)

add_executable(audio_qa_recording_replay_preparation_test
    recording_replay_preparation_test.cpp)
target_link_libraries(audio_qa_recording_replay_preparation_test PRIVATE
    ayther_audio_qa_domain zstd::libzstd)
target_compile_features(audio_qa_recording_replay_preparation_test PRIVATE
    cxx_std_20)
set_target_properties(audio_qa_recording_replay_preparation_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_recording_replay_preparation_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_recording_replay_preparation_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_recording_replay_preparation_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_recording_replay_preparation
    COMMAND audio_qa_recording_replay_preparation_test)
set_tests_properties(audio_qa_recording_replay_preparation PROPERTIES
    LABELS "audio_qa;contract" TIMEOUT 10)

add_executable(audio_qa_recording_size_limits_test
    recording_size_limits_test.cpp)
target_link_libraries(audio_qa_recording_size_limits_test PRIVATE
    ayther_audio_qa_domain zstd::libzstd)
target_compile_features(audio_qa_recording_size_limits_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_recording_size_limits_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_recording_size_limits_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_recording_size_limits_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_recording_size_limits_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_recording_size_limits
    COMMAND audio_qa_recording_size_limits_test)
set_tests_properties(audio_qa_recording_size_limits PROPERTIES
    LABELS "audio_qa;contract;limits" TIMEOUT 30)

add_executable(audio_qa_replay_duration_limit_test
    replay_duration_limit_test.cpp)
target_link_libraries(audio_qa_replay_duration_limit_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_replay_duration_limit_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_replay_duration_limit_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_replay_duration_limit_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_replay_duration_limit_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_replay_duration_limit_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_replay_duration_limit
    COMMAND audio_qa_replay_duration_limit_test)
set_tests_properties(audio_qa_replay_duration_limit PROPERTIES
    LABELS "audio_qa;unit;limits" TIMEOUT 10)

add_executable(audio_qa_replay_start_policy_test replay_start_policy_test.cpp)
target_link_libraries(audio_qa_replay_start_policy_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_replay_start_policy_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_replay_start_policy_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_replay_start_policy_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_replay_start_policy_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_replay_start_policy_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_replay_start_policy
    COMMAND audio_qa_replay_start_policy_test)
set_tests_properties(audio_qa_replay_start_policy PROPERTIES
    LABELS "audio_qa;contract" TIMEOUT 10)

add_executable(audio_qa_hd_initialization_test hd_initialization_test.cpp)
target_link_libraries(audio_qa_hd_initialization_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_hd_initialization_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_hd_initialization_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_hd_initialization_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_hd_initialization_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_hd_initialization_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_hd_initialization
    COMMAND audio_qa_hd_initialization_test)
set_tests_properties(audio_qa_hd_initialization PROPERTIES
    LABELS "audio_qa;contract" TIMEOUT 10)

add_executable(audio_qa_initial_state_publication_test
    initial_state_publication_test.cpp)
target_link_libraries(audio_qa_initial_state_publication_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_initial_state_publication_test PRIVATE
    cxx_std_20)
set_target_properties(audio_qa_initial_state_publication_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_initial_state_publication_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_initial_state_publication_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_initial_state_publication_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_initial_state_publication
    COMMAND audio_qa_initial_state_publication_test)
set_tests_properties(audio_qa_initial_state_publication PROPERTIES
    LABELS "audio_qa;contract" TIMEOUT 10)

add_executable(audio_qa_recording_replay_loop_test
    recording_replay_loop_test.cpp)
target_link_libraries(audio_qa_recording_replay_loop_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_recording_replay_loop_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_recording_replay_loop_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_recording_replay_loop_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_recording_replay_loop_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_recording_replay_loop_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_recording_replay_loop
    COMMAND audio_qa_recording_replay_loop_test)
set_tests_properties(audio_qa_recording_replay_loop PROPERTIES
    LABELS "audio_qa;contract" TIMEOUT 10)

add_executable(audio_qa_replay_control_gate_test
    replay_control_gate_test.cpp)
target_link_libraries(audio_qa_replay_control_gate_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_replay_control_gate_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_replay_control_gate_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_replay_control_gate_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_replay_control_gate_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_replay_control_gate_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_replay_control_gate
    COMMAND audio_qa_replay_control_gate_test)
set_tests_properties(audio_qa_replay_control_gate PROPERTIES
    LABELS "audio_qa;contract" TIMEOUT 10)

add_executable(audio_qa_replay_condition_lock_test
    replay_condition_lock_test.cpp)
target_link_libraries(audio_qa_replay_condition_lock_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_replay_condition_lock_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_replay_condition_lock_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_replay_condition_lock_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_replay_condition_lock_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_replay_condition_lock_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_replay_condition_lock
    COMMAND audio_qa_replay_condition_lock_test)
set_tests_properties(audio_qa_replay_condition_lock PROPERTIES
    LABELS "audio_qa;contract" TIMEOUT 10)

add_executable(audio_qa_replay_progress_test replay_progress_test.cpp)
target_link_libraries(audio_qa_replay_progress_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_replay_progress_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_replay_progress_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_replay_progress_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_replay_progress_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_replay_progress_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_replay_progress COMMAND audio_qa_replay_progress_test)
set_tests_properties(audio_qa_replay_progress PROPERTIES
    LABELS "audio_qa;contract" TIMEOUT 10)

add_executable(audio_qa_replay_production_close_test
    replay_production_close_test.cpp)
target_link_libraries(audio_qa_replay_production_close_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_replay_production_close_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_replay_production_close_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_replay_production_close_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_replay_production_close_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_replay_production_close_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_replay_production_close
    COMMAND audio_qa_replay_production_close_test)
set_tests_properties(audio_qa_replay_production_close PROPERTIES
    LABELS "audio_qa;contract" TIMEOUT 10)

add_executable(audio_qa_run_lifecycle_test run_lifecycle_test.cpp)
target_link_libraries(audio_qa_run_lifecycle_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_run_lifecycle_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_run_lifecycle_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_run_lifecycle_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_run_lifecycle_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_run_lifecycle_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_run_lifecycle COMMAND audio_qa_run_lifecycle_test)
set_tests_properties(audio_qa_run_lifecycle PROPERTIES
    LABELS "audio_qa;unit" TIMEOUT 10)

add_executable(audio_qa_run_result_separation_test
    run_result_separation_test.cpp)
target_link_libraries(audio_qa_run_result_separation_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_run_result_separation_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_run_result_separation_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_run_result_separation_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_run_result_separation_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_run_result_separation_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_run_result_separation
    COMMAND audio_qa_run_result_separation_test)
set_tests_properties(audio_qa_run_result_separation PROPERTIES
    LABELS "audio_qa;unit" TIMEOUT 10)

add_executable(audio_qa_session_occupancy_test session_occupancy_test.cpp)
target_link_libraries(audio_qa_session_occupancy_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_session_occupancy_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_session_occupancy_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_session_occupancy_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_session_occupancy_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_session_occupancy_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_session_occupancy
    COMMAND audio_qa_session_occupancy_test)
set_tests_properties(audio_qa_session_occupancy PROPERTIES
    LABELS "audio_qa;unit" TIMEOUT 10)

add_executable(audio_qa_session_cessation_gate_test
    session_cessation_gate_test.cpp)
target_link_libraries(audio_qa_session_cessation_gate_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_session_cessation_gate_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_session_cessation_gate_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_session_cessation_gate_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_session_cessation_gate_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_session_cessation_gate_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_session_cessation_gate
    COMMAND audio_qa_session_cessation_gate_test)
set_tests_properties(audio_qa_session_cessation_gate PROPERTIES
    LABELS "audio_qa;unit" TIMEOUT 10)

add_executable(audio_qa_request_registry_test request_registry_test.cpp)
target_link_libraries(audio_qa_request_registry_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_request_registry_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_request_registry_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_request_registry_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_request_registry_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_request_registry_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_request_registry COMMAND audio_qa_request_registry_test)
set_tests_properties(audio_qa_request_registry PROPERTIES
    LABELS "audio_qa;unit" TIMEOUT 10)

add_executable(audio_qa_intentional_repetition_test
    intentional_repetition_test.cpp)
target_link_libraries(audio_qa_intentional_repetition_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_intentional_repetition_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_intentional_repetition_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_intentional_repetition_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_intentional_repetition_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_intentional_repetition_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_intentional_repetition
    COMMAND audio_qa_intentional_repetition_test)
set_tests_properties(audio_qa_intentional_repetition PROPERTIES
    LABELS "audio_qa;unit" TIMEOUT 10)

add_executable(audio_qa_take_run_sequence_test take_run_sequence_test.cpp)
target_link_libraries(audio_qa_take_run_sequence_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_take_run_sequence_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_take_run_sequence_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_take_run_sequence_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_take_run_sequence_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_take_run_sequence_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_take_run_sequence COMMAND audio_qa_take_run_sequence_test)
set_tests_properties(audio_qa_take_run_sequence PROPERTIES
    LABELS "audio_qa;unit" TIMEOUT 10)

add_executable(audio_qa_take_process_sequence_test
    take_process_sequence_test.cpp)
target_link_libraries(audio_qa_take_process_sequence_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_take_process_sequence_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_take_process_sequence_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_take_process_sequence_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_take_process_sequence_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_take_process_sequence_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_take_process_sequence
    COMMAND audio_qa_take_process_sequence_test)
set_tests_properties(audio_qa_take_process_sequence PROPERTIES
    LABELS "audio_qa;unit" TIMEOUT 10)

add_executable(audio_qa_replay_cancellation_test replay_cancellation_test.cpp)
target_link_libraries(audio_qa_replay_cancellation_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_replay_cancellation_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_replay_cancellation_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_replay_cancellation_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_replay_cancellation_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_replay_cancellation_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_replay_cancellation
    COMMAND audio_qa_replay_cancellation_test)
set_tests_properties(audio_qa_replay_cancellation PROPERTIES
    LABELS "audio_qa;integration" TIMEOUT 10)

add_executable(audio_qa_replay_terminal_arbiter_test
    replay_terminal_arbiter_test.cpp)
target_link_libraries(audio_qa_replay_terminal_arbiter_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_replay_terminal_arbiter_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_replay_terminal_arbiter_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_replay_terminal_arbiter_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_replay_terminal_arbiter_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_replay_terminal_arbiter_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_replay_terminal_arbiter
    COMMAND audio_qa_replay_terminal_arbiter_test)
set_tests_properties(audio_qa_replay_terminal_arbiter PROPERTIES
    LABELS "audio_qa;unit" TIMEOUT 10)

add_executable(audio_qa_evidence_close_test evidence_close_test.cpp)
target_link_libraries(audio_qa_evidence_close_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_evidence_close_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_evidence_close_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_evidence_close_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_evidence_close_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_evidence_close_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_evidence_close COMMAND audio_qa_evidence_close_test)
set_tests_properties(audio_qa_evidence_close PROPERTIES
    LABELS "audio_qa;unit" TIMEOUT 10)

add_executable(audio_qa_process_cessation_test process_cessation_test.cpp)
target_link_libraries(audio_qa_process_cessation_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_process_cessation_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_process_cessation_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_process_cessation_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_process_cessation_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_process_cessation_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_process_cessation
    COMMAND audio_qa_process_cessation_test)
set_tests_properties(audio_qa_process_cessation PROPERTIES
    LABELS "audio_qa;unit" TIMEOUT 10)

add_executable(audio_qa_capability_gate_test capability_gate_test.cpp)
target_link_libraries(audio_qa_capability_gate_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_capability_gate_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_capability_gate_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_capability_gate_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_capability_gate_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_capability_gate_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_capability_gate COMMAND audio_qa_capability_gate_test)
set_tests_properties(audio_qa_capability_gate PROPERTIES
    LABELS "audio_qa;unit" TIMEOUT 10)

add_executable(audio_qa_capability_report_test capability_report_test.cpp)
target_link_libraries(audio_qa_capability_report_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_capability_report_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_capability_report_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_capability_report_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_capability_report_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_capability_report_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_capability_report
    COMMAND audio_qa_capability_report_test)
set_tests_properties(audio_qa_capability_report PROPERTIES
    LABELS "audio_qa;unit;protocol" TIMEOUT 10)

add_executable(audio_qa_preparation_watchdog_test
    preparation_watchdog_test.cpp)
target_link_libraries(audio_qa_preparation_watchdog_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_preparation_watchdog_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_preparation_watchdog_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_preparation_watchdog_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_preparation_watchdog_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_preparation_watchdog_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_preparation_watchdog
    COMMAND audio_qa_preparation_watchdog_test)
set_tests_properties(audio_qa_preparation_watchdog PROPERTIES
    LABELS "audio_qa;unit" TIMEOUT 10)

add_executable(audio_qa_playback_watchdog_test
    playback_watchdog_test.cpp)
target_link_libraries(audio_qa_playback_watchdog_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_playback_watchdog_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_playback_watchdog_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_playback_watchdog_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_playback_watchdog_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_playback_watchdog_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_playback_watchdog
    COMMAND audio_qa_playback_watchdog_test)
set_tests_properties(audio_qa_playback_watchdog PROPERTIES
    LABELS "audio_qa;unit" TIMEOUT 10)

add_executable(audio_qa_closing_watchdog_test
    closing_watchdog_test.cpp)
target_link_libraries(audio_qa_closing_watchdog_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_closing_watchdog_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_closing_watchdog_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_closing_watchdog_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_closing_watchdog_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_closing_watchdog_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_closing_watchdog
    COMMAND audio_qa_closing_watchdog_test)
set_tests_properties(audio_qa_closing_watchdog PROPERTIES
    LABELS "audio_qa;unit" TIMEOUT 10)

add_executable(audio_qa_cancellation_window_test
    cancellation_window_test.cpp)
target_link_libraries(audio_qa_cancellation_window_test PRIVATE
    ayther_audio_qa_domain)
target_compile_features(audio_qa_cancellation_window_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_cancellation_window_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_cancellation_window_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_cancellation_window_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_cancellation_window_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_cancellation_window
    COMMAND audio_qa_cancellation_window_test)
set_tests_properties(audio_qa_cancellation_window PROPERTIES
    LABELS "audio_qa;unit" TIMEOUT 10)

add_executable(audio_qa_control_message_test control_message_test.cpp)
target_link_libraries(audio_qa_control_message_test PRIVATE
    ayther_audio_qa_model_toml)
target_compile_features(audio_qa_control_message_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_control_message_test PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(audio_qa_control_message_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_control_message_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_control_message COMMAND audio_qa_control_message_test)
set_tests_properties(audio_qa_control_message PROPERTIES
    LABELS "audio_qa;contract" TIMEOUT 10)

add_executable(audio_qa_fact_batch_test fact_batch_test.cpp)
target_link_libraries(audio_qa_fact_batch_test PRIVATE
    ayther_audio_qa_model_toml)
target_compile_features(audio_qa_fact_batch_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_fact_batch_test PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(audio_qa_fact_batch_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_fact_batch_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_fact_batch COMMAND audio_qa_fact_batch_test)
set_tests_properties(audio_qa_fact_batch PROPERTIES
    LABELS "audio_qa;contract" TIMEOUT 10)

add_executable(audio_qa_pcm_message_test pcm_message_test.cpp)
target_link_libraries(audio_qa_pcm_message_test PRIVATE
    ayther_audio_qa_model_toml)
target_compile_features(audio_qa_pcm_message_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_pcm_message_test PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(audio_qa_pcm_message_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_pcm_message_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_pcm_message COMMAND audio_qa_pcm_message_test)
set_tests_properties(audio_qa_pcm_message PROPERTIES
    LABELS "audio_qa;contract" TIMEOUT 10)

add_executable(audio_qa_pcm_capture_limits_test pcm_capture_limits_test.cpp)
target_link_libraries(audio_qa_pcm_capture_limits_test PRIVATE
    ayther_audio_qa_model_toml)
target_compile_features(audio_qa_pcm_capture_limits_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_pcm_capture_limits_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(audio_qa_pcm_capture_limits_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_pcm_capture_limits_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME audio_qa_pcm_capture_limits
    COMMAND audio_qa_pcm_capture_limits_test)
set_tests_properties(audio_qa_pcm_capture_limits PROPERTIES
    LABELS "audio_qa;integration;limits" TIMEOUT 10)

add_executable(audio_qa_evidence_quota_test evidence_quota_test.cpp)
target_link_libraries(audio_qa_evidence_quota_test PRIVATE
    ayther_audio_qa_evidence)
target_compile_features(audio_qa_evidence_quota_test PRIVATE cxx_std_20)
set_target_properties(audio_qa_evidence_quota_test PROPERTIES
    CXX_EXTENSIONS OFF)
if(AYTHER_CLANG_TIDY_COMMAND)
    set_target_properties(audio_qa_evidence_quota_test PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
if(MSVC)
    target_compile_options(audio_qa_evidence_quota_test PRIVATE
        /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_evidence_quota_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
if(WIN32)
    add_custom_command(TARGET audio_qa_evidence_quota_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${qa_toml_dll_debug},${qa_toml_dll_release}>"
            "$<TARGET_FILE_DIR:audio_qa_evidence_quota_test>" VERBATIM)
endif()
add_test(NAME audio_qa_evidence_quota
    COMMAND audio_qa_evidence_quota_test)
set_tests_properties(audio_qa_evidence_quota PROPERTIES
    LABELS "audio_qa;integration;limits;fault" TIMEOUT 10)

if(WIN32)
    add_executable(audio_qa_inherited_channel_test inherited_channel_test.cpp)
    target_link_libraries(audio_qa_inherited_channel_test PRIVATE
        ayther_audio_qa_transport)
    target_compile_features(audio_qa_inherited_channel_test PRIVATE cxx_std_20)
    set_target_properties(audio_qa_inherited_channel_test PROPERTIES
        CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(audio_qa_inherited_channel_test PRIVATE
            /W4 /WX /permissive- /EHsc)
    else()
        target_compile_options(audio_qa_inherited_channel_test PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME audio_qa_inherited_channel
        COMMAND audio_qa_inherited_channel_test)
    set_tests_properties(audio_qa_inherited_channel PROPERTIES
        LABELS "audio_qa;integration" TIMEOUT 15)

    add_executable(audio_qa_channel_disconnect_latency_test
        channel_disconnect_latency_test.cpp)
    target_link_libraries(audio_qa_channel_disconnect_latency_test PRIVATE
        ayther_audio_qa_transport)
    target_compile_features(audio_qa_channel_disconnect_latency_test PRIVATE
        cxx_std_20)
    set_target_properties(audio_qa_channel_disconnect_latency_test PROPERTIES
        CXX_EXTENSIONS OFF)
    if(AYTHER_CLANG_TIDY_COMMAND)
        set_target_properties(audio_qa_channel_disconnect_latency_test PROPERTIES
            CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
    endif()
    if(MSVC)
        target_compile_options(audio_qa_channel_disconnect_latency_test PRIVATE
            /W4 /WX /permissive- /EHsc)
    else()
        target_compile_options(audio_qa_channel_disconnect_latency_test PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME audio_qa_channel_disconnect_latency
        COMMAND audio_qa_channel_disconnect_latency_test)
    set_tests_properties(audio_qa_channel_disconnect_latency PROPERTIES
        LABELS "audio_qa;integration;limits;fault" TIMEOUT 15)

    add_executable(audio_qa_runtime_process_test runtime_process_test.cpp)
    target_link_libraries(audio_qa_runtime_process_test PRIVATE
        ayther_audio_qa_transport)
    target_compile_features(audio_qa_runtime_process_test PRIVATE cxx_std_20)
    set_target_properties(audio_qa_runtime_process_test PROPERTIES
        CXX_EXTENSIONS OFF)
    if(AYTHER_CLANG_TIDY_COMMAND)
        set_target_properties(audio_qa_runtime_process_test PROPERTIES
            CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
    endif()
    if(MSVC)
        target_compile_options(audio_qa_runtime_process_test PRIVATE
            /W4 /WX /permissive- /EHsc)
    else()
        target_compile_options(audio_qa_runtime_process_test PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME audio_qa_runtime_process COMMAND audio_qa_runtime_process_test)
    set_tests_properties(audio_qa_runtime_process PROPERTIES
        LABELS "audio_qa;integration" TIMEOUT 15)

    add_executable(audio_qa_separated_channel_drain_test
        separated_channel_drain_test.cpp)
    target_link_libraries(audio_qa_separated_channel_drain_test PRIVATE
        ayther_audio_qa_transport)
    target_compile_features(audio_qa_separated_channel_drain_test PRIVATE
        cxx_std_20)
    set_target_properties(audio_qa_separated_channel_drain_test PROPERTIES
        CXX_EXTENSIONS OFF)
    if(AYTHER_CLANG_TIDY_COMMAND)
        set_target_properties(audio_qa_separated_channel_drain_test PROPERTIES
            CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
    endif()
    if(MSVC)
        target_compile_options(audio_qa_separated_channel_drain_test PRIVATE
            /W4 /WX /permissive- /EHsc)
    else()
        target_compile_options(audio_qa_separated_channel_drain_test PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME audio_qa_separated_channel_drain
        COMMAND audio_qa_separated_channel_drain_test)
    set_tests_properties(audio_qa_separated_channel_drain PROPERTIES
        LABELS "audio_qa;integration" TIMEOUT 15)

    add_executable(audio_qa_cancellation_message_test
        cancellation_message_test.cpp)
    target_link_libraries(audio_qa_cancellation_message_test PRIVATE
        ayther_audio_qa_model_toml ayther_audio_qa_transport)
    target_compile_features(audio_qa_cancellation_message_test PRIVATE
        cxx_std_20)
    set_target_properties(audio_qa_cancellation_message_test PROPERTIES
        CXX_EXTENSIONS OFF)
    if(AYTHER_CLANG_TIDY_COMMAND)
        set_target_properties(audio_qa_cancellation_message_test PROPERTIES
            CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
    endif()
    if(MSVC)
        target_compile_options(audio_qa_cancellation_message_test PRIVATE
            /W4 /WX /permissive- /EHsc)
    else()
        target_compile_options(audio_qa_cancellation_message_test PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME audio_qa_cancellation_message
        COMMAND audio_qa_cancellation_message_test)
    set_tests_properties(audio_qa_cancellation_message PROPERTIES
        LABELS "audio_qa;integration;protocol" TIMEOUT 15)
endif()

# Input preparation only. No link to Engine, Runtime or an audio backend.
add_executable(audio_qa_known_pcm_fixture known_pcm_fixture.cpp)
target_compile_features(audio_qa_known_pcm_fixture PRIVATE cxx_std_20)
set_target_properties(audio_qa_known_pcm_fixture PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(audio_qa_known_pcm_fixture PRIVATE /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(audio_qa_known_pcm_fixture PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

foreach(mode IN ITEMS block errors)
    add_test(NAME audio_qa_known_pcm_${mode}
        COMMAND ${CMAKE_COMMAND}
            "-DFIXTURE_EXE=$<TARGET_FILE:audio_qa_known_pcm_fixture>"
            "-DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/results/$<CONFIG>"
            "-DMODE=${mode}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/known_pcm_test.cmake")
    set_tests_properties(audio_qa_known_pcm_${mode} PROPERTIES
        LABELS "audio_qa;fixture" TIMEOUT 15)
endforeach()

if(AYTHER_CLANG_TIDY_COMMAND)
    get_property(audio_qa_build_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(audio_qa_target IN LISTS audio_qa_build_targets)
        if(audio_qa_target MATCHES "^audio_qa_")
            get_target_property(audio_qa_target_tidy
                ${audio_qa_target} CXX_CLANG_TIDY)
            if(NOT audio_qa_target_tidy)
                message(FATAL_ERROR
                    "Audio QA target '${audio_qa_target}' is outside clang-tidy")
            endif()
        endif()
    endforeach()
endif()
