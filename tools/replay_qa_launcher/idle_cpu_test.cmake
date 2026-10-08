# Campaign 2026-10-07 (RNF-2, D-6b): the launcher stays open while the Runtime presents the
# replay. Idle, it must wait for input instead of redrawing as fast as it can.
# Usage: cmake -DLAUNCHER=<ayther_replay_qa> -P idle_cpu_test.cmake
set(seconds 3)
execute_process(COMMAND "${LAUNCHER}" --idle-seconds ${seconds}
    RESULT_VARIABLE code OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 60)
if(NOT code EQUAL 0 OR NOT output MATCHES "idle frames=([0-9]+) cpu_ms=([0-9]+)")
    message(FATAL_ERROR "the launcher did not report its idle use (${code}):\n${output}${errors}")
endif()
set(frames ${CMAKE_MATCH_1})
set(cpu_ms ${CMAKE_MATCH_2})
# At most one redraw per display refresh, and well under a quarter of a core.
math(EXPR max_frames "${seconds} * 75")
math(EXPR max_cpu_ms "${seconds} * 250")
if(frames GREATER max_frames OR cpu_ms GREATER max_cpu_ms)
    message(FATAL_ERROR
        "the idle launcher redraws ${frames} frames using ${cpu_ms} ms of CPU in ${seconds} s")
endif()
message(STATUS "idle launcher: ${frames} frames, ${cpu_ms} ms of CPU in ${seconds} s")
