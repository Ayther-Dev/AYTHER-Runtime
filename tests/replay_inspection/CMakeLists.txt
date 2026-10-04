# Spec 002 (plan §3, §5.4-§5.10): pure modules of the replay inspection. Each module has
# its test without GPU, SDL or disk.
function(replay_inspection_test name)
    add_executable(replay_inspection_${name}_tests ${name}_test.cpp)
    target_link_libraries(replay_inspection_${name}_tests PRIVATE ayther_replay_inspection)
    set_target_properties(replay_inspection_${name}_tests PROPERTIES
        CXX_EXTENSIONS OFF RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    add_test(NAME replay_inspection_${name} COMMAND replay_inspection_${name}_tests)
    set_tests_properties(replay_inspection_${name} PROPERTIES
        LABELS "replay_inspection;unit" TIMEOUT 10)
endfunction()

replay_inspection_test(take_clock)
replay_inspection_test(key_router)
replay_inspection_test(inspection_state)
replay_inspection_test(presentation_health)
replay_inspection_test(checkpoint_ring)
replay_inspection_test(frame_record)
replay_inspection_test(replay_messages)
replay_inspection_test(render_observation)
replay_inspection_test(input_script)
replay_inspection_test(debug_view)
replay_inspection_test(av_sync)
