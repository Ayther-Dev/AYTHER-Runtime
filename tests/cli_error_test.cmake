if(NOT DEFINED RUNTIME_EXE OR RUNTIME_EXE STREQUAL "")
    message(FATAL_ERROR "RUNTIME_EXE is required")
endif()

function(assert_cli_error case_name expected_diagnostic)
    execute_process(
        COMMAND "${RUNTIME_EXE}" ${ARGN}
        RESULT_VARIABLE actual_code
        OUTPUT_VARIABLE actual_stdout
        ERROR_VARIABLE actual_stderr
        ENCODING UTF-8
    )
    if(NOT "${actual_code}" STREQUAL "64")
        message(FATAL_ERROR
            "${case_name}: expected exit code 64, got ${actual_code}\n"
            "stdout:\n${actual_stdout}\n"
            "stderr:\n${actual_stderr}")
    endif()
    string(FIND "${actual_stderr}" "${expected_diagnostic}" diagnostic_at)
    if(diagnostic_at EQUAL -1)
        message(FATAL_ERROR
            "${case_name}: stderr does not identify '${expected_diagnostic}'\n"
            "stderr:\n${actual_stderr}")
    endif()
endfunction()

assert_cli_error(subsystems_missing "--subsystems" --subsystems)
assert_cli_error(mute_buses_invalid_sign "--mute-buses" --mute-buses +1)
assert_cli_error(frames_overflow "--frames" --frames 18446744073709551616)
assert_cli_error(capture_malformed "--capture-at" --capture-at 1,,2)
assert_cli_error(required_session_arguments "Usage:")
