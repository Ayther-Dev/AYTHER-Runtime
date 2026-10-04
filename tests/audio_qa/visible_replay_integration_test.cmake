cmake_minimum_required(VERSION 3.25)

# Reuse the public fixture and verify the headless baseline first.
include("${CMAKE_CURRENT_LIST_DIR}/real_replay_integration_test.cmake")
# Spec 002 (RF-2.8): the last visible take stays paused at N−1 after its natural end; the
# runs below close the window there through the scripted input of the QA tests.
file(WRITE "${TEST_ROOT}/close-at-end.script" "paused=5 close\n")
set(ENV{AYTHER_QA_INPUT_SCRIPT} "${TEST_ROOT}/close-at-end.script")
execute_process(
    COMMAND "${CHECK_EXE}" check
        --runtime "${RUNTIME_EXE}" --rom "${ROM_FILE}" --reference "${REFERENCE}"
        --play-manifest "${MANIFEST}" --pack "${PACK_FILE}"
        --trust-registry "${TRUST_REGISTRY}" --take "${TAKE_FILE}"
        --output "${TEST_ROOT}/visible" --request-id visible-public
        --presentation visible --language es
    RESULT_VARIABLE visible_result OUTPUT_VARIABLE visible_out ERROR_VARIABLE visible_err
    TIMEOUT 30)
set(visible_report "${visible_out}${visible_err}")
if(NOT visible_result EQUAL 0 AND NOT visible_result EQUAL 2)
    message(FATAL_ERROR "Visible replay failed: ${visible_report}")
endif()
foreach(expected IN ITEMS "presentation=visible" "presented_frames=6" "inputs_consumed=6"
                          "relationships_reopened=true" "audible_restart_observed=not_evaluated"
                          "final_state_sha256=${final_hash}")
    if(NOT visible_report MATCHES "${expected}")
        message(FATAL_ERROR "Visible replay omitted ${expected}: ${visible_report}")
    endif()
endforeach()
unset(ENV{SDL_AUDIO_DRIVER})
unset(ENV{SDL_VIDEO_DRIVER})
set(long_take "${TEST_ROOT}/long-public.arp")
execute_process(COMMAND "${FIXTURE_EXE}" "${long_take}" --long
    RESULT_VARIABLE fixture_result)
if(NOT fixture_result EQUAL 0)
    message(FATAL_ERROR "Long cancellation fixture failed")
endif()
execute_process(COMMAND pwsh -NoProfile -File
    "${CMAKE_CURRENT_LIST_DIR}/visible_cancellation_test.ps1"
    -Checker "${CHECK_EXE}" -Runtime "${RUNTIME_EXE}" -Rom "${ROM_FILE}"
    -Reference "${REFERENCE}"
    -Manifest "${MANIFEST}" -Pack "${PACK_FILE}" -Trust "${TRUST_REGISTRY}"
    -Take "${long_take}" -OutputRoot "${TEST_ROOT}/cancelled"
    RESULT_VARIABLE cancellation_result OUTPUT_VARIABLE cancellation_output
    ERROR_VARIABLE cancellation_error TIMEOUT 35)
if(NOT cancellation_result EQUAL 0)
    message(FATAL_ERROR "Visible cancellation failed: ${cancellation_output}${cancellation_error}")
endif()
if(visible_report MATCHES "audio_backend=(dummy|disk)" OR
   visible_report MATCHES "presentation_status=(video_|window_|renderer_|audible_|postprocess_)")
    message(FATAL_ERROR "Visible replay was not presented normally: ${visible_report}")
endif()
file(GLOB terminals "${TEST_ROOT}/visible/runs/*/replay-result.toml")
list(LENGTH terminals terminal_count)
if(NOT terminal_count EQUAL 1)
    message(FATAL_ERROR "Visible terminal was not durably published")
endif()
list(GET terminals 0 terminal)
file(READ "${terminal}" contents)
if(NOT contents MATCHES "schema = '1.4'" OR NOT contents MATCHES "mode = 'visible'" OR
   NOT contents MATCHES "presented_frames = 6")
    message(FATAL_ERROR "Presentation metadata did not survive reopening: ${contents}")
endif()
message(STATUS "Visible and headless replay produced identical final game state; ${visible_report}")

foreach(failure IN ITEMS video audio)
    if(failure STREQUAL "video")
        set(ENV{SDL_VIDEO_DRIVER} "dummy")
        set(expected_failure "window_creation_failed")
    else()
        unset(ENV{SDL_VIDEO_DRIVER})
        set(ENV{SDL_AUDIO_DRIVER} "dummy")
        set(expected_failure "audible_output_unavailable")
    endif()
    execute_process(
        COMMAND "${CHECK_EXE}" check
            --runtime "${RUNTIME_EXE}" --rom "${ROM_FILE}" --reference "${REFERENCE}"
            --play-manifest "${MANIFEST}" --pack "${PACK_FILE}"
            --trust-registry "${TRUST_REGISTRY}" --take "${TAKE_FILE}"
            --output "${TEST_ROOT}/${failure}-failure" --request-id "${failure}-failure"
            --presentation visible
        RESULT_VARIABLE failed_code OUTPUT_VARIABLE failed_out ERROR_VARIABLE failed_err
        TIMEOUT 30)
    set(failure_report "${failed_out}${failed_err}")
    if(NOT failed_code EQUAL 2 OR NOT failure_report MATCHES "inputs_consumed=6" OR
       NOT failure_report MATCHES "presentation_status=${expected_failure}" OR
       NOT failure_report MATCHES "audible_restart_observed=not_evaluated")
        message(FATAL_ERROR "Presentation failure lost replay or diagnostic: ${failure_report}")
    endif()
    file(GLOB failure_terminals "${TEST_ROOT}/${failure}-failure/runs/*/replay-result.toml")
    list(LENGTH failure_terminals failure_terminal_count)
    if(NOT failure_terminal_count EQUAL 1)
        message(FATAL_ERROR "Presentation failure terminal was not preserved")
    endif()
    file(READ "${TEST_ROOT}/${failure}-failure/request-ledger.toml" failure_ledger)
    if(NOT failure_ledger MATCHES "playback_result = 'natural_end'" OR
       NOT failure_ledger MATCHES "evidence_result = 'incomplete'")
        message(FATAL_ERROR "Presentation degradation changed the playback outcome")
    endif()
endforeach()

unset(ENV{SDL_AUDIO_DRIVER})
unset(ENV{SDL_VIDEO_DRIVER})
file(COPY_FILE "${TAKE_FILE}" "${TEST_ROOT}/second-public.arp")
execute_process(COMMAND "${CHECK_EXE}" check
    --runtime "${RUNTIME_EXE}" --rom "${ROM_FILE}" --reference "${REFERENCE}"
    --play-manifest "${MANIFEST}" --pack "${PACK_FILE}"
    --trust-registry "${TRUST_REGISTRY}" --take "${TAKE_FILE}"
    --take "${TEST_ROOT}/second-public.arp" --output "${TEST_ROOT}/sequential"
    --request-id sequential-visible --presentation visible
    RESULT_VARIABLE sequential_code OUTPUT_VARIABLE sequential_out ERROR_VARIABLE sequential_err
    TIMEOUT 30)
set(sequential_report "${sequential_out}${sequential_err}")
string(REGEX MATCHALL "presented_frames=6" presented_takes "${sequential_report}")
string(REGEX MATCHALL "relationships_reopened=true" reopened_takes "${sequential_report}")
list(LENGTH presented_takes presented_count)
list(LENGTH reopened_takes reopened_count)
if((NOT sequential_code EQUAL 0 AND NOT sequential_code EQUAL 2) OR
   NOT presented_count EQUAL 2 OR NOT reopened_count EQUAL 2)
    message(FATAL_ERROR "Sequential visible takes failed: ${sequential_report}")
endif()

# Spec 002, BR-149 (RF-2.8, RF-2.9, RF-4.8): the natural end of the last take confirms the
# linear result before staying paused at N−1 with the window alive; an intermediate take
# moves on unless a pause was pending, and then Space starts the next take.
function(read_terminals root out)
    file(GLOB_RECURSE terminals "${root}/runs/*/replay-result.toml")
    list(SORT terminals)
    set(${out} "${terminals}" PARENT_SCOPE)
endfunction()

file(WRITE "${TEST_ROOT}/ended-paused.script"
    "paused=5 key space down\nafter=0 key space up\nafter=1000 close\n")
set(ENV{AYTHER_QA_INPUT_SCRIPT} "${TEST_ROOT}/ended-paused.script")
string(TIMESTAMP ended_started "%s" UTC)
execute_process(COMMAND "${CHECK_EXE}" check
    --runtime "${RUNTIME_EXE}" --rom "${ROM_FILE}" --reference "${REFERENCE}"
    --play-manifest "${MANIFEST}" --pack "${PACK_FILE}"
    --trust-registry "${TRUST_REGISTRY}" --take "${TAKE_FILE}"
    --output "${TEST_ROOT}/ended-paused" --request-id ended-paused --presentation visible
    RESULT_VARIABLE ended_code OUTPUT_VARIABLE ended_out ERROR_VARIABLE ended_err TIMEOUT 30)
string(TIMESTAMP ended_finished "%s" UTC)
math(EXPR ended_seconds "${ended_finished} - ${ended_started}")
read_terminals("${TEST_ROOT}/ended-paused" ended_terminals)
list(LENGTH ended_terminals ended_count)
if(NOT ended_code EQUAL 0 OR NOT ended_count EQUAL 1 OR ended_seconds LESS 1)
    message(FATAL_ERROR "RF-2.8: the last take did not stay paused at its end "
                        "(${ended_code}, ${ended_count}, ${ended_seconds}s): ${ended_out}${ended_err}")
endif()
file(READ "${ended_terminals}" ended_terminal)
foreach(expected IN ITEMS "ended_paused = true" "playback = 'natural_end'" "traversal = 'linear'"
                          "linear_completed = true")
    if(NOT ended_terminal MATCHES "${expected}")
        message(FATAL_ERROR "RF-2.8: the confirmed result lacks ${expected}: ${ended_terminal}")
    endif()
endforeach()

file(WRITE "${TEST_ROOT}/pending-pause.script"
    "frame=4 key space down\nafter=0 key space up\npaused=5 key other up\n"
    "after=300 key space down\nafter=0 key space up\nafter=500 close\n")
set(ENV{AYTHER_QA_INPUT_SCRIPT} "${TEST_ROOT}/pending-pause.script")
execute_process(COMMAND "${CHECK_EXE}" check
    --runtime "${RUNTIME_EXE}" --rom "${ROM_FILE}" --reference "${REFERENCE}"
    --play-manifest "${MANIFEST}" --pack "${PACK_FILE}"
    --trust-registry "${TRUST_REGISTRY}" --take "${TAKE_FILE}"
    --take "${TEST_ROOT}/second-public.arp" --output "${TEST_ROOT}/pending-pause"
    --request-id pending-pause --presentation visible
    RESULT_VARIABLE pending_code OUTPUT_VARIABLE pending_out ERROR_VARIABLE pending_err TIMEOUT 40)
read_terminals("${TEST_ROOT}/pending-pause" pending_terminals)
list(LENGTH pending_terminals pending_count)
if(NOT pending_code EQUAL 0 OR NOT pending_count EQUAL 2)
    message(FATAL_ERROR "RF-4.8: two takes with a pending pause did not both end "
                        "(${pending_code}, ${pending_count}): ${pending_out}${pending_err}")
endif()
foreach(terminal IN LISTS pending_terminals)
    file(READ "${terminal}" contents)
    if(NOT contents MATCHES "ended_paused = true" OR
       NOT contents MATCHES "playback = 'natural_end'" OR
       NOT contents MATCHES "traversal = 'linear'" OR
       contents MATCHES "user_pause_ms = '0'")
        message(FATAL_ERROR "RF-4.8: a take paused at its end was not reported so: ${contents}")
    endif()
endforeach()

# Spec 002, BR-150 (RF-2.9): navigating after the natural end of the last take opens a
# post-end inspection with its own run; the confirmed result stays as it was.
file(WRITE "${TEST_ROOT}/post-end.script"
    "paused=5 key left down\nafter=0 key left up\npaused=4 key other up\nafter=300 close\n")
set(ENV{AYTHER_QA_INPUT_SCRIPT} "${TEST_ROOT}/post-end.script")
execute_process(COMMAND "${CHECK_EXE}" check
    --runtime "${RUNTIME_EXE}" --rom "${ROM_FILE}" --reference "${REFERENCE}"
    --play-manifest "${MANIFEST}" --pack "${PACK_FILE}"
    --trust-registry "${TRUST_REGISTRY}" --take "${TAKE_FILE}"
    --output "${TEST_ROOT}/post-end" --request-id post-end --presentation visible
    RESULT_VARIABLE post_code OUTPUT_VARIABLE post_out ERROR_VARIABLE post_err TIMEOUT 30)
file(GLOB post_linear "${TEST_ROOT}/post-end/runs/*/replay-result.toml")
file(GLOB post_inspection "${TEST_ROOT}/post-end/runs/*-inspection-1/replay-result.toml")
list(LENGTH post_linear post_count)
list(LENGTH post_inspection post_inspection_count)
if(NOT post_code EQUAL 0 OR NOT post_count EQUAL 2 OR NOT post_inspection_count EQUAL 1)
    message(FATAL_ERROR "RF-2.9: the post-end inspection was not kept apart "
                        "(${post_code}, ${post_count}, ${post_inspection_count}): ${post_out}${post_err}")
endif()
file(READ "${post_inspection}" post_terminal)
if(NOT post_terminal MATCHES "traversal = 'post_end_inspection'")
    message(FATAL_ERROR "RF-2.9: the post-end run is not a post-end inspection: ${post_terminal}")
endif()
foreach(terminal IN LISTS post_linear)
    if(NOT terminal MATCHES "-inspection-1")
        file(READ "${terminal}" linear_terminal)
        if(NOT linear_terminal MATCHES "traversal = 'linear'" OR
           NOT linear_terminal MATCHES "playback = 'natural_end'")
            message(FATAL_ERROR "RF-2.9: the confirmed result changed: ${linear_terminal}")
        endif()
    endif()
endforeach()
file(GLOB post_summary "${TEST_ROOT}/post-end/requests/*/request-summary.toml")
file(READ "${post_summary}" post_summary_text)
if(NOT post_summary_text MATCHES "traversal = 'linear'")
    message(FATAL_ERROR "RF-2.9: the request summary changed: ${post_summary_text}")
endif()
unset(ENV{AYTHER_QA_INPUT_SCRIPT})
message(STATUS "BR-149: ended paused after ${ended_seconds}s; two takes with a pending pause")
message(STATUS "BR-150: the post-end inspection has its own run")
