if(NOT DEFINED CHECK_EXE OR CHECK_EXE STREQUAL "")
    message(FATAL_ERROR "CHECK_EXE is required")
endif()
if(NOT DEFINED TEST_ROOT OR TEST_ROOT STREQUAL "")
    message(FATAL_ERROR "TEST_ROOT is required")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(REMOVE_RECURSE "${TEST_ROOT}-summary")
file(REMOVE_RECURSE "${TEST_ROOT}-es")
file(REMOVE_RECURSE "${TEST_ROOT}-en")
file(MAKE_DIRECTORY "${TEST_ROOT}")

function(assert_invocation expected_result expected_pattern)
    execute_process(
        COMMAND "${CHECK_EXE}" ${ARGN}
        RESULT_VARIABLE actual_result
        OUTPUT_VARIABLE standard_output
        ERROR_VARIABLE standard_error
    )
    if(NOT actual_result EQUAL expected_result)
        message(FATAL_ERROR
            "Unexpected result ${actual_result}; expected ${expected_result}. "
            "stdout='${standard_output}' stderr='${standard_error}'")
    endif()
    string(CONCAT combined_output "${standard_output}" "${standard_error}")
    if(NOT combined_output MATCHES "${expected_pattern}")
        message(FATAL_ERROR
            "Missing diagnostic '${expected_pattern}' in '${combined_output}'")
    endif()
endfunction()

function(assert_localized_invocation expected_language output_root request_id)
    set(language_arguments)
    if(expected_language STREQUAL "en")
        list(APPEND language_arguments --language en)
    endif()
    execute_process(
        COMMAND "${CHECK_EXE}" check
            --runtime runtime.exe
            --reference reference.toml
            --play-manifest play.toml
            --pack pack.ay
            --output "${output_root}"
            --request-id "${request_id}"
            ${language_arguments}
        RESULT_VARIABLE actual_result
        OUTPUT_VARIABLE standard_output
        ERROR_VARIABLE standard_error
    )
    if(NOT actual_result EQUAL 2)
        message(FATAL_ERROR
            "Unexpected localized result ${actual_result}; expected 2. "
            "stdout='${standard_output}' stderr='${standard_error}'")
    endif()
    string(CONCAT combined_output "${standard_output}" "${standard_error}")
    foreach(expected_pattern IN ITEMS
            "play_manifest_unavailable"
            "audio_qa_message\\[${expected_language}\\]")
        if(NOT combined_output MATCHES "${expected_pattern}")
            message(FATAL_ERROR
                "Missing localized diagnostic '${expected_pattern}' in "
                "'${combined_output}'")
        endif()
    endforeach()
endfunction()

assert_invocation(3 "invalid_invocation")
assert_invocation(3 "invalid_invocation" inspect)
assert_invocation(3 "missing_required_option: --runtime" check)
assert_invocation(2 "play_manifest_unavailable" check
    --runtime runtime.exe
    --reference reference.toml
    --play-manifest play.toml
    --pack pack.ay
    --output "${TEST_ROOT}"
    --request-id request-entry)
assert_invocation(2 "audio_qa_summary: exit_code=2 complete=0 incomplete=1" check
    --runtime runtime.exe
    --reference reference.toml
    --play-manifest play.toml
    --pack pack.ay
    --output "${TEST_ROOT}-summary"
    --request-id request-entry-summary)
assert_invocation(2 "request_known: request-entry" check
    --runtime runtime.exe
    --reference reference.toml
    --play-manifest play.toml
    --pack pack.ay
    --output "${TEST_ROOT}"
    --request-id request-entry)
assert_invocation(3 "request_identity_conflict: request-entry" check
    --runtime runtime.exe
    --reference reference.toml
    --play-manifest play.toml
    --pack changed.ay
    --output "${TEST_ROOT}"
    --request-id request-entry)
assert_invocation(2 "play_manifest_unavailable" check
    --runtime runtime.exe
    --reference reference.toml
    --play-manifest play.toml
    --pack pack.ay
    --output "${TEST_ROOT}"
    --request-id request-other)

assert_localized_invocation(es "${TEST_ROOT}-es" request-es)
assert_localized_invocation(en "${TEST_ROOT}-en" request-en)

file(REMOVE_RECURSE "${TEST_ROOT}")
file(REMOVE_RECURSE "${TEST_ROOT}-summary")
file(REMOVE_RECURSE "${TEST_ROOT}-es")
file(REMOVE_RECURSE "${TEST_ROOT}-en")
