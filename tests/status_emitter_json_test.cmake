if(NOT DEFINED STATUS_EMITTER_TEST_EXE OR STATUS_EMITTER_TEST_EXE STREQUAL "")
    message(FATAL_ERROR "STATUS_EMITTER_TEST_EXE is required")
endif()

execute_process(
    COMMAND "${STATUS_EMITTER_TEST_EXE}" --json-fixture
    RESULT_VARIABLE fixture_code
    OUTPUT_VARIABLE fixture_output
    ERROR_VARIABLE fixture_error
    ENCODING UTF-8
)
if(NOT fixture_code EQUAL 0)
    message(FATAL_ERROR
        "status fixture failed with ${fixture_code}:\n${fixture_error}")
endif()

string(REGEX MATCHALL "AYTHER_STATUS \\{[^\r\n]*\\}" status_lines
       "${fixture_output}")
list(LENGTH status_lines status_count)
if(NOT status_count EQUAL 8)
    message(FATAL_ERROR
        "expected 8 complete status lines, found ${status_count}:\n${fixture_output}")
endif()

set(event_names "")
foreach(status_line IN LISTS status_lines)
    string(REGEX REPLACE "^AYTHER_STATUS " "" json "${status_line}")
    string(JSON event_name GET "${json}" event)
    list(APPEND event_names "${event_name}")

    if(event_name STREQUAL "ready")
        string(JSON has_pack_type TYPE "${json}" has_pack)
        if(NOT has_pack_type STREQUAL "BOOLEAN")
            message(FATAL_ERROR "ready.has_pack is not a JSON boolean: ${json}")
        endif()
        string(JSON decoded_game_id GET "${json}" game_id)
        string(ASCII 10 newline)
        set(expected_game_id "quote \" slash\\ control")
        string(ASCII 1 control_one)
        string(APPEND expected_game_id "${control_one} newline${newline}UTF-8: español 日本")
        if(NOT decoded_game_id STREQUAL expected_game_id)
            message(FATAL_ERROR "ready.game_id did not round-trip through JSON")
        endif()
    endif()
endforeach()

foreach(required_event probe ready now-playing warning crash-test exit)
    if(NOT required_event IN_LIST event_names)
        message(FATAL_ERROR "fixture did not cover '${required_event}'")
    endif()
endforeach()
