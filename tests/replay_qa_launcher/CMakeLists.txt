# Spec 002 (plan §5.14): the pure parts of the launcher, tested without GPU.
function(replay_qa_launcher_test name)
    add_executable(replay_qa_launcher_${name}_tests ${name}_test.cpp)
    target_link_libraries(replay_qa_launcher_${name}_tests PRIVATE ayther_replay_qa_model)
    set_target_properties(replay_qa_launcher_${name}_tests PROPERTIES
        CXX_EXTENSIONS OFF RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    if(WIN32)
        add_custom_command(TARGET replay_qa_launcher_${name}_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_RUNTIME_DLLS:replay_qa_launcher_${name}_tests>"
                "$<TARGET_FILE_DIR:replay_qa_launcher_${name}_tests>"
            COMMAND_EXPAND_LISTS VERBATIM)
    endif()
    add_test(NAME replay_qa_launcher_${name} COMMAND replay_qa_launcher_${name}_tests ${ARGN})
    set_tests_properties(replay_qa_launcher_${name} PROPERTIES
        LABELS "qa;launcher;unit" TIMEOUT 30)
endfunction()

replay_qa_launcher_test(launcher_messages)
replay_qa_launcher_test(launcher_model)
replay_qa_launcher_test(environment_resolver)
replay_qa_launcher_test(launcher_runner)
replay_qa_launcher_test(launcher_view)
# BR-171: regenerates the correspondence with the beta.8 inventory in the build tree.
replay_qa_launcher_test(option_coverage
    "${CMAKE_CURRENT_LIST_DIR}/../audio_qa/fixtures/rf1-option-inventory-beta8.toml"
    "${CMAKE_BINARY_DIR}/evidence/rf1-option-correspondence.md")
set_property(TEST replay_qa_launcher_option_coverage APPEND PROPERTY LABELS contract)
