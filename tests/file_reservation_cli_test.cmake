file(MAKE_DIRECTORY "${TEST_DIR}")
file(WRITE "${TEST_DIR}/rom.bin" "fixture")
foreach(kind IN ITEMS rom pack)
    execute_process(
        COMMAND "${RUNTIME_EXE}" --core unused-core
            --rom "${TEST_DIR}/rom.bin" --${kind}-revision wrong-revision
        RESULT_VARIABLE code OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    if(NOT code EQUAL 74 OR output MATCHES "AYTHER_RESERVATION 1"
       OR NOT output MATCHES "runtime.reservation_failed")
        message(FATAL_ERROR "Invalid ${kind} reservation was not rejected: ${code}: ${output} ${errors}")
    endif()
endforeach()
execute_process(
    COMMAND "${RUNTIME_EXE}" --core unused-core --rom unused-rom
        --trust-registry "${TEST_DIR}/missing.toml"
    RESULT_VARIABLE code OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT code EQUAL 78 OR output MATCHES "AYTHER_RESERVATION 1")
    message(FATAL_ERROR "Launch without reservations changed: ${code}: ${output} ${errors}")
endif()
