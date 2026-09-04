cmake_minimum_required(VERSION 3.21)

foreach(required IN ITEMS STATUS_EMITTER_TEST_EXE CONFORMANCE_SCRIPT OUTPUT_FILE)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

execute_process(
    COMMAND "${STATUS_EMITTER_TEST_EXE}" --json-fixture
    RESULT_VARIABLE emitter_result
    OUTPUT_VARIABLE emitter_output
    ERROR_VARIABLE emitter_error
    ENCODING UTF-8)
if(NOT emitter_result EQUAL 0)
    message(FATAL_ERROR "emitter fixture failed: ${emitter_error}")
endif()
file(WRITE "${OUTPUT_FILE}" "${emitter_output}")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
            "-DINPUT_FILE=${OUTPUT_FILE}"
            -DEXPECT_VALID=ON
            -P "${CONFORMANCE_SCRIPT}"
    RESULT_VARIABLE conformance_result
    OUTPUT_VARIABLE conformance_output
    ERROR_VARIABLE conformance_error)
if(NOT conformance_result EQUAL 0)
    message(FATAL_ERROR
        "Runtime output failed shared conformance:\n"
        "${conformance_output}\n${conformance_error}")
endif()
