# Spec 008 (RF-4.1, TP-304.4): a save state written by another emulator
# generation does not start the session: exit 65 and the stable reason
# state.generation_mismatch, before the first frame.
if(NOT DEFINED RUNTIME_EXE OR NOT DEFINED TEST_CORE OR NOT DEFINED STATE_FIXTURE
   OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "RUNTIME_EXE, TEST_CORE, STATE_FIXTURE and WORK_DIR are required")
endif()
if(NOT EXISTS "${TEST_CORE}")
    message("SKIPPED: the Engine package has no ${TEST_CORE}")
    cmake_language(EXIT 77)
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")
set(rom "${WORK_DIR}/content.md")
file(WRITE "${rom}" "AYTHER state generation fixture content\n")

execute_process(
    COMMAND "${RUNTIME_EXE}" --core "${TEST_CORE}" --rom "${rom}"
            --load-state "${STATE_FIXTURE}"
    RESULT_VARIABLE actual_code
    OUTPUT_VARIABLE actual_stdout
    ERROR_VARIABLE actual_stderr
    TIMEOUT 60
    ENCODING UTF-8)
if(NOT "${actual_code}" STREQUAL "65")
    message(FATAL_ERROR "expected exit code 65, got ${actual_code}\n"
                        "stdout:\n${actual_stdout}\nstderr:\n${actual_stderr}")
endif()
string(FIND "${actual_stdout}" "\"reason\":\"state.generation_mismatch\"" reason_at)
string(FIND "${actual_stdout}" "\"event\":\"ready\"" ready_at)
if(reason_at EQUAL -1 OR NOT ready_at EQUAL -1)
    message(FATAL_ERROR "stdout must carry state.generation_mismatch and no ready\n"
                        "stdout:\n${actual_stdout}")
endif()
