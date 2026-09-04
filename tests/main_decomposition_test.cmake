cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED MAIN_SOURCE OR NOT EXISTS "${MAIN_SOURCE}")
    message(FATAL_ERROR "MAIN_SOURCE must name src/main.cpp")
endif()

file(READ "${MAIN_SOURCE}" main_source)
string(REGEX MATCHALL "\n" main_newlines "${main_source}")
list(LENGTH main_newlines main_newline_count)
math(EXPR main_line_count "${main_newline_count} + 1")

if(main_line_count GREATER 20)
    message(FATAL_ERROR
        "src/main.cpp must remain a composition-only entrypoint; "
        "found ${main_line_count} lines")
endif()

foreach(forbidden IN ITEMS SDL_ vk capture_write player_config AytherSession)
    string(FIND "${main_source}" "${forbidden}" forbidden_at)
    if(NOT forbidden_at EQUAL -1)
        message(FATAL_ERROR
            "src/main.cpp leaked implementation concern '${forbidden}'")
    endif()
endforeach()

string(FIND "${main_source}" "run_runtime" run_at)
if(run_at EQUAL -1)
    message(FATAL_ERROR "src/main.cpp does not delegate to run_runtime")
endif()
