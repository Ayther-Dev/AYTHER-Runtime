# Spec 002 (plan §3): pure modules of the replay inspection. They include only `std`, their
# own types and the header-only observation contracts of the Engine package (C3, BR-134): no
# SDL, ImGui, Vulkan, disk or processes, and they do not link the Engine.
add_library(ayther_replay_inspection STATIC
    av_sync.cpp checkpoint_ring.cpp debug_view.cpp frame_record.cpp input_script.cpp inspection_state.cpp key_router.cpp
    presentation_health.cpp
    recovery_planner.cpp render_observation.cpp replay_messages.cpp take_clock.cpp)
target_include_directories(ayther_replay_inspection PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    "$<TARGET_PROPERTY:Ayther::engine,INTERFACE_INCLUDE_DIRECTORIES>")
target_compile_features(ayther_replay_inspection PUBLIC cxx_std_20)
set_target_properties(ayther_replay_inspection PROPERTIES CXX_EXTENSIONS OFF)
if(AYTHER_ENABLE_CLANG_TIDY)
    set_target_properties(ayther_replay_inspection PROPERTIES
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()
