cmake_minimum_required(VERSION 3.25)

# Reuse the public fixture and verify the headless baseline first.
include("${CMAKE_CURRENT_LIST_DIR}/real_replay_integration_test.cmake")
execute_process(
    COMMAND "${CHECK_EXE}" check
        --runtime "${RUNTIME_EXE}" --reference "${REFERENCE}"
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
    -Checker "${CHECK_EXE}" -Runtime "${RUNTIME_EXE}" -Reference "${REFERENCE}"
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
if(NOT contents MATCHES "schema = '1.3'" OR NOT contents MATCHES "mode = 'visible'" OR
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
            --runtime "${RUNTIME_EXE}" --reference "${REFERENCE}"
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
    --runtime "${RUNTIME_EXE}" --reference "${REFERENCE}"
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
