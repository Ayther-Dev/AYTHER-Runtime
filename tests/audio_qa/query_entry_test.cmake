if(NOT DEFINED CHECK_EXE OR CHECK_EXE STREQUAL "")
    message(FATAL_ERROR "CHECK_EXE is required")
endif()
if(NOT DEFINED QUERY_INDEX OR QUERY_INDEX STREQUAL "")
    message(FATAL_ERROR "QUERY_INDEX is required")
endif()

function(run_query expected_result expected_language expected_status sequence)
    set(language_arguments)
    if(expected_language STREQUAL "en")
        list(APPEND language_arguments --language en)
    endif()
    execute_process(
        COMMAND "${CHECK_EXE}" query
            --index "${QUERY_INDEX}"
            --run-id run-query
            --producer-id detector
            --sequence "${sequence}"
            ${language_arguments}
        RESULT_VARIABLE actual_result
        OUTPUT_VARIABLE standard_output
        ERROR_VARIABLE standard_error
    )
    string(CONCAT combined_output "${standard_output}" "${standard_error}")
    if(NOT actual_result EQUAL expected_result OR
       NOT combined_output MATCHES "status=${expected_status}" OR
       NOT combined_output MATCHES "audio_qa_message\\[${expected_language}\\]")
        message(FATAL_ERROR
            "Unexpected query result=${actual_result} output='${combined_output}'")
    endif()
    foreach(forbidden IN ITEMS bug_status confirmed score resolved confirmado puntua resuelto)
        if(combined_output MATCHES "${forbidden}")
            message(FATAL_ERROR
                "Query output contains forbidden conclusion '${forbidden}'")
        endif()
    endforeach()
endfunction()

run_query(0 es found 1)
run_query(2 en not_found 99)
