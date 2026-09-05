if(NOT DEFINED RUNTIME_EXE OR RUNTIME_EXE STREQUAL "")
    message(FATAL_ERROR "RUNTIME_EXE is required")
endif()
if(NOT DEFINED TEST_DIR OR TEST_DIR STREQUAL "")
    message(FATAL_ERROR "TEST_DIR is required")
endif()

file(MAKE_DIRECTORY "${TEST_DIR}")
set(invalid_map "${TEST_DIR}/invalid-input-map.toml")
file(WRITE "${invalid_map}" "[keyboard]\na = \"not an SDL key\"\n")

execute_process(
    COMMAND "${RUNTIME_EXE}"
            --core unused-core --rom unused-rom --input-map "${invalid_map}"
    RESULT_VARIABLE actual_code
    OUTPUT_VARIABLE actual_stdout
    ERROR_VARIABLE actual_stderr
    ENCODING UTF-8
)

if(NOT "${actual_code}" STREQUAL "78")
    message(FATAL_ERROR
        "expected input-map exit code 78, got ${actual_code}\n"
        "stdout:\n${actual_stdout}\n"
        "stderr:\n${actual_stderr}")
endif()
string(FIND "${actual_stdout}" "\"reason\":\"input.map_invalid\"" reason_at)
if(reason_at EQUAL -1)
    message(FATAL_ERROR
        "stdout lacks stable input.map_invalid reason\nstdout:\n${actual_stdout}")
endif()
string(FIND "${actual_stderr}" "--input-map" diagnostic_at)
if(diagnostic_at EQUAL -1)
    message(FATAL_ERROR
        "stderr does not identify --input-map\nstderr:\n${actual_stderr}")
endif()

# Configuration failures must happen before any SDL or Vulkan startup work.
string(FIND "${actual_stdout}" "[tiempo]" startup_milestone_at)
string(FIND "${actual_stderr}" "SDL_Init(" sdl_error_at)
if(NOT startup_milestone_at EQUAL -1 OR NOT sdl_error_at EQUAL -1)
    message(FATAL_ERROR
        "invalid input map reached SDL/Vulkan startup\n"
        "stdout:\n${actual_stdout}\nstderr:\n${actual_stderr}")
endif()
