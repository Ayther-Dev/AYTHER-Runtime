cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED RUNTIME_EXE OR NOT EXISTS "${RUNTIME_EXE}")
    message(FATAL_ERROR "RUNTIME_EXE must name the built Runtime")
endif()

execute_process(
    COMMAND "${RUNTIME_EXE}" --qa-session
            --core "must-not-be-opened.dll" --rom "must-not-be-opened.rom"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors
    TIMEOUT 10)

if(NOT result EQUAL 65)
    message(FATAL_ERROR
        "QA session entry returned ${result}, expected protocol exit 65\n"
        "stdout:\n${output}\nstderr:\n${errors}")
endif()

set(expected
    "AYTHER_QA_SESSION {\"schema\":\"1.0\",\"status\":\"unavailable\",\"reason\":\"qa.control_decoder_unavailable\"}")
string(FIND "${output}" "${expected}" entry_at)
if(entry_at EQUAL -1)
    message(FATAL_ERROR "QA session entry omitted its explicit status:\n${output}")
endif()

foreach(forbidden IN ITEMS
        "AYTHER_RESERVATION"
        "SDL_Init"
        "must-not-be-opened.dll"
        "must-not-be-opened.rom"
        "AYTHER_STATUS")
    string(FIND "${output}${errors}" "${forbidden}" forbidden_at)
    if(NOT forbidden_at EQUAL -1)
        message(FATAL_ERROR "QA session fell into ordinary launch: ${forbidden}")
    endif()
endforeach()

message(STATUS "QA session mode remains isolated from ordinary launch")
