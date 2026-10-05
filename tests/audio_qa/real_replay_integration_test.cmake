cmake_minimum_required(VERSION 3.25)

foreach(required IN ITEMS CHECK_EXE RUNTIME_EXE CORE_DLL ROM_FILE PACK_FILE
                          TRUST_REGISTRY TAKE_FILE TEST_ROOT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required for the real replay test")
    endif()
    if(NOT "${required}" STREQUAL "TEST_ROOT" AND NOT EXISTS "${${required}}")
        message(FATAL_ERROR "${required} does not exist: ${${required}}")
    endif()
endforeach()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")
set(USER_APPDATA "${TEST_ROOT}/user-appdata")
file(MAKE_DIRECTORY "${USER_APPDATA}/Ayther/saves")
file(WRITE "${USER_APPDATA}/Ayther/saves/user-marker.sav" "ordinary-user-save")
set(ENV{APPDATA} "${USER_APPDATA}")
foreach(path_name IN ITEMS CORE_DLL ROM_FILE PACK_FILE)
    file(TO_CMAKE_PATH "${${path_name}}" ${path_name}_TOML)
endforeach()
set(MANIFEST "${TEST_ROOT}/launch.toml")
file(WRITE "${MANIFEST}"
    "format = 1\n"
    "id = \"qa170-public\"\n"
    "created = \"2026-09-29T00:00:00Z\"\n"
    "play_version = \"0.1.0\"\n"
    "rom = \"${ROM_FILE_TOML}\"\n"
    "core = \"${CORE_DLL_TOML}\"\n"
    "pack = \"${PACK_FILE_TOML}\"\n")
set(REFERENCE "${TEST_ROOT}/reference.toml")
file(WRITE "${REFERENCE}" [=[
schema_version = 1
schema_minor = 0
kind = "reference"
baseline_id = "qa170-public"
execution_reference_id = "qa170-public-execution"
role = "initial"
materials = []
conditions = []
declared_differences = []

[engine.artifact]
origin = "unknown"
evidence_id = ""
available = false
[engine.release]
origin = "unknown"
evidence_id = ""
available = false
[engine.commit]
origin = "unknown"
evidence_id = ""
available = false
[engine.variant]
origin = "unknown"
evidence_id = ""
available = false
[engine.abi]
origin = "unknown"
evidence_id = ""
available = false

[runtime.artifact]
origin = "unknown"
evidence_id = ""
available = false
[runtime.release]
origin = "unknown"
evidence_id = ""
available = false
[runtime.commit]
origin = "unknown"
evidence_id = ""
available = false
[runtime.variant]
origin = "unknown"
evidence_id = ""
available = false
[runtime.abi]
origin = "unknown"
evidence_id = ""
available = false

[conditions_manifest_id]
origin = "artifact_manifest"
evidence_id = "qa170-public-manifest"
available = true
value = "qa170-public"
]=])

execute_process(
    COMMAND "${CHECK_EXE}" check
        --runtime "${RUNTIME_EXE}"
        --rom "${ROM_FILE}"
        --reference "${REFERENCE}"
        --play-manifest "${MANIFEST}"
        --pack "${PACK_FILE}"
        --trust-registry "${TRUST_REGISTRY}"
        --take "${TAKE_FILE}"
        --output "${TEST_ROOT}/evidence"
        --request-id "qa170-public-request"
        --language es
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors
    TIMEOUT 30)

if(NOT result EQUAL 0)
    message(FATAL_ERROR
        "Real replay returned ${result}, expected complete technical evidence (0)\n"
        "stdout:\n${output}\nstderr:\n${errors}")
endif()
set(report "${output}${errors}")
foreach(expected IN ITEMS
        "recording_frames=6"
        "inputs_consumed=6"
        "assignments=1"
        "occurrence=1"
        "ingress=3:"
        "candidate=5:"
        "selection=5:"
        "request=5:"
        "decision=5:"
        "effect=5:"
        "mix_span=6:"
        # Spec 002 (BR-146, contracts.md C2; DI-12): 123 engine facts, one
        # audio_frame_output_boundary per frame that reaches the device (Engine rc.15) and one
        # render_frame per frame: 123 + 6 + 6.
        "durable_facts=135"
        "durable_pcm_blocks="
        "fact_integrity_complete=true"
        "relationships_reopened=true"
        "runtime_data_isolated=true"
        "status=replay_evidence_reopened"
        "outcome=complete"
        "diagnostic=replay_evidence_complete"
        "audible_restart_observed=not_evaluated"
        "audible_overlap_observed=not_evaluated")
    string(FIND "${report}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Real replay omitted ${expected}:\n${report}")
    endif()
endforeach()

execute_process(
    COMMAND "${CHECK_EXE}" check
        --runtime "${RUNTIME_EXE}"
        --rom "${ROM_FILE}"
        --reference "${REFERENCE}"
        --play-manifest "${MANIFEST}"
        --pack "${PACK_FILE}"
        --pack-mode original
        --trust-registry "${TRUST_REGISTRY}"
        --take "${TAKE_FILE}"
        --output "${TEST_ROOT}/original-evidence"
        --request-id "qa170-public-original-request"
        --language es
    RESULT_VARIABLE original_result
    OUTPUT_VARIABLE original_output
    ERROR_VARIABLE original_errors
    TIMEOUT 30)
if(NOT original_result EQUAL 0)
    message(FATAL_ERROR "Original-audio replay returned ${original_result}:\n${original_output}\n${original_errors}")
endif()
set(original_report "${original_output}${original_errors}")
foreach(expected IN ITEMS "inputs_consumed=6" "assignments=0" "outcome=complete"
                          "diagnostic=replay_evidence_complete")
    string(FIND "${original_report}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Original-audio replay omitted ${expected}:\n${original_report}")
    endif()
endforeach()
file(READ "${TEST_ROOT}/evidence/request-ledger.toml" ledger)
foreach(expected IN ITEMS
        "playback_result = 'natural_end'"
        "evidence_result = 'complete'")
    string(FIND "${ledger}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Real replay ledger omitted ${expected}:\n${ledger}")
    endif()
endforeach()
file(READ "${USER_APPDATA}/Ayther/saves/user-marker.sav" user_save)
if(NOT user_save STREQUAL "ordinary-user-save" OR
   EXISTS "${USER_APPDATA}/Ayther/runtime")
    message(FATAL_ERROR
        "Real replay reached ordinary user data: marker=${user_save}")
endif()
file(GLOB private_runtime_roots "${TEST_ROOT}/evidence/.runtime-data-*")
if(private_runtime_roots)
    message(FATAL_ERROR
        "Real replay retained private Runtime data: ${private_runtime_roots}")
endif()
file(GLOB fact_fragments "${TEST_ROOT}/evidence/runs/*/fragments/*.aqf")
file(GLOB pcm_blocks "${TEST_ROOT}/evidence/runs/*/audio/*.aqp")
list(LENGTH fact_fragments fact_fragment_count)
list(LENGTH pcm_blocks pcm_block_count)
if(fact_fragment_count LESS 1 OR pcm_block_count LESS 1)
    message(FATAL_ERROR
        "Integrated evidence files were not published: facts=${fact_fragment_count}, pcm=${pcm_block_count}")
endif()
string(REGEX MATCH "initial_state_sha256=([0-9a-f]+)" initial "${report}")
set(initial_hash "${CMAKE_MATCH_1}")
string(REGEX MATCH "final_state_sha256=([0-9a-f]+)" final "${report}")
set(final_hash "${CMAKE_MATCH_1}")
string(LENGTH "${initial_hash}" initial_length)
string(LENGTH "${final_hash}" final_length)
if(NOT initial_length EQUAL 64 OR NOT final_length EQUAL 64 OR
   initial_hash STREQUAL final_hash)
    message(FATAL_ERROR
        "Real replay did not preserve distinct initial/final states:\n${report}")
endif()

execute_process(
    COMMAND "${CHECK_EXE}" check
        --runtime "${RUNTIME_EXE}"
        --rom "${ROM_FILE}"
        --reference "${REFERENCE}"
        --play-manifest "${MANIFEST}"
        --pack "${PACK_FILE}"
        --trust-registry "${TEST_ROOT}/missing-trust.toml"
        --take "${TAKE_FILE}"
        --output "${TEST_ROOT}/failed-evidence"
        --request-id "qa170-missing-trust"
        --language es
    RESULT_VARIABLE failed_result
    OUTPUT_VARIABLE failed_output
    ERROR_VARIABLE failed_errors
    TIMEOUT 30)
set(failed_report "${failed_output}${failed_errors}")
# Spec 002 (RF-2.1, RF-2.2, plan §5.2): a missing trust registry is a material error
# found before admission; the request ends with 3 and never reaches the Runtime, where
# it used to fail later as an empty audio catalog (`audio_assignment_catalog_empty`, no
# longer a failure: a valid pack without audio catalog replays with zero assignments).
if(NOT failed_result EQUAL 3 OR
   NOT failed_report MATCHES "material_not_found: --trust-registry" OR
   EXISTS "${TEST_ROOT}/failed-evidence")
    message(FATAL_ERROR
        "A missing trust registry was not rejected before admission: result=${failed_result}\n"
        "${failed_report}")
endif()

# Spec 002, BR-072 (RF-1.3, RF-1.4, RF-2.3, RF-2.5), without GPU. A material changed
# between validation and its take needs a hook in the middle of the request and is
# covered by audio_qa_check_runner_integration (RF-2.11).
function(spec002_check expected output_root)
    execute_process(
        COMMAND "${CHECK_EXE}" check --runtime "${RUNTIME_EXE}" --rom "${ROM_FILE}"
                --core "${CORE_DLL}" --output "${output_root}" --language es ${ARGN}
        RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 60)
    if(NOT code EQUAL expected)
        message(FATAL_ERROR "spec 002 check returned ${code}, expected ${expected}:\n${out}${err}")
    endif()
    set(spec002_report "${out}${err}" PARENT_SCOPE)
endfunction()

# RF-1.3: without pack the take replays the ROM alone.
spec002_check(0 "${TEST_ROOT}/spec002" --take "${TAKE_FILE}" --request-id spec002-nopack)
foreach(expected IN ITEMS "pack=none source=explicit" "assignments=0" "exit_code=0")
    if(NOT spec002_report MATCHES "${expected}")
        message(FATAL_ERROR "No-pack request omitted ${expected}:\n${spec002_report}")
    endif()
endforeach()

# BR-074 (RF-5.8, RF-2.13): each run keeps its traversal; without navigation it is one
# linear segment over every frame consumed.
file(GLOB traversals "${TEST_ROOT}/spec002/runs/*/traversal.toml")
list(LENGTH traversals traversal_count)
if(NOT traversal_count EQUAL 1)
    message(FATAL_ERROR "The run has no traversal.toml: ${traversals}")
endif()
file(READ "${traversals}" traversal_text)
foreach(expected IN ITEMS "schema_minor = 2" "kind = 'linear'" "linear_completed = true"
                          "frames_total = 6" "from = 0" "to = 5")
    string(FIND "${traversal_text}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "traversal.toml omitted ${expected}:
${traversal_text}")
    endif()
endforeach()

# Spec 002, D-9 (DI-14): an inspection keeps its audio per linear segment. Pause at 3, step back
# to 2 (a checkpoint is restored and the PCM line goes back), resume: the PCM before the pause
# and the PCM after the resume are two contiguous segments. The facts of the frames produced
# silently are not evidence either (plan D14): D-11 (DI-15) declares them as exclusions of the
# recovery, and the evidence of the take is complete. The request ends with 0 (contracts.md C5:
# every take ended naturally with complete evidence), and the traversal is an inspection, never
# accredited as linear (`linear_complete = false`).
file(WRITE "${TEST_ROOT}/inspection.script"
    "frame=3 key space down\nafter=0 key space up\npaused=3 key left down\nafter=0 key left up\n"
    "paused=2 key space down\nafter=0 key space up\n")
set(ENV{AYTHER_QA_INPUT_SCRIPT} "${TEST_ROOT}/inspection.script")
spec002_check(0 "${TEST_ROOT}/spec002-inspection" --take "${TAKE_FILE}"
              --request-id spec002-inspection)
unset(ENV{AYTHER_QA_INPUT_SCRIPT})
if(spec002_report MATCHES "evidence_error=" OR
   NOT spec002_report MATCHES "durable_pcm_blocks=[1-9]" OR
   NOT spec002_report MATCHES "playback=natural_end traversal=inspection")
    message(FATAL_ERROR "D-9: an inspection did not keep complete audio evidence:\n${spec002_report}")
endif()
file(GLOB inspection_traversals "${TEST_ROOT}/spec002-inspection/runs/*/traversal.toml")
file(READ "${inspection_traversals}" inspection_traversal)
foreach(expected IN ITEMS "schema_minor = 2" "kind = 'inspection'" "linear_completed = false"
                          "[[audio_segments]]" "segment = 1" "frame_from = 3" "frame_to = 5"
                          "[[fact_exclusions]]" "recovery = 1" "cause = 'silent_recovery'")
    string(FIND "${inspection_traversal}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "D-9, D-11: traversal.toml omitted ${expected}:\n${inspection_traversal}")
    endif()
endforeach()
# D-11 (DI-15): the declared exclusions are not losses: the evidence of the inspection is
# complete, and the take is still not accredited as linear.
function(spec002_inspection_complete root what)
    file(GLOB summary "${root}/requests/*/request-summary.toml")
    file(READ "${summary}" summary_text)
    if(NOT summary_text MATCHES "traversal = 'inspection'" OR
       NOT summary_text MATCHES "linear_complete = false" OR
       NOT summary_text MATCHES "playback = 'natural_end'" OR
       NOT summary_text MATCHES "evidence = 'complete'" OR
       summary_text MATCHES "data_lost|fragments_not_flushed|pcm_")
        message(FATAL_ERROR "D-11, DI-15: the evidence of an inspection ${what} is not complete:\n"
                            "${summary_text}")
    endif()
endfunction()
spec002_inspection_complete("${TEST_ROOT}/spec002-inspection" "without pack")
# The same with the pack: its relationships reopen as well.
set(ENV{AYTHER_QA_INPUT_SCRIPT} "${TEST_ROOT}/inspection.script")
spec002_check(0 "${TEST_ROOT}/spec002-inspection-pack" --take "${TAKE_FILE}"
              --pack "${PACK_FILE}" --trust-registry "${TRUST_REGISTRY}"
              --request-id spec002-inspection-pack)
unset(ENV{AYTHER_QA_INPUT_SCRIPT})
if(spec002_report MATCHES "evidence_error=" OR
   NOT spec002_report MATCHES "assignments=1" OR
   NOT spec002_report MATCHES "durable_pcm_blocks=[1-9]" OR
   NOT spec002_report MATCHES "playback=natural_end traversal=inspection")
    message(FATAL_ERROR "D-9: an inspection with pack lost its audio evidence:\n${spec002_report}")
endif()
spec002_inspection_complete("${TEST_ROOT}/spec002-inspection-pack" "with pack")

# RF-2.3: the same request returns its confirmed summary without starting the Runtime.
spec002_check(0 "${TEST_ROOT}/spec002" --take "${TAKE_FILE}" --request-id spec002-nopack)
if(NOT spec002_report MATCHES "request_known: spec002-nopack" OR
   spec002_report MATCHES "audio_qa_replay:")
    message(FATAL_ERROR "A known request was not answered from its summary:\n${spec002_report}")
endif()

# RF-1.4: an explicit repetition runs twice, one run per position.
spec002_check(0 "${TEST_ROOT}/spec002-repeat" --take "${TAKE_FILE}" --take "${TAKE_FILE}"
              --request-id spec002-repeat)
string(REGEX MATCHALL "audio_qa_replay: run_id=[^ ]+ take=[^ ]+ position=[01]" replays
       "${spec002_report}")
list(LENGTH replays replay_count)
file(GLOB repeated_runs "${TEST_ROOT}/spec002-repeat/runs/*")
list(LENGTH repeated_runs repeated_run_count)
if(NOT replay_count EQUAL 2 OR NOT repeated_run_count EQUAL 2)
    message(FATAL_ERROR "The repetition did not run twice:\n${spec002_report}")
endif()

# RF-2.5: [A, B fails, C] keeps A, closes B with its diagnostic and leaves C unstarted.
if(DEFINED BROKEN_TAKE_FILE AND EXISTS "${BROKEN_TAKE_FILE}")
    spec002_check(2 "${TEST_ROOT}/spec002-failure" --take "${TAKE_FILE}"
                  --take "${BROKEN_TAKE_FILE}" --take "${TAKE_FILE}" --request-id spec002-failure)
    foreach(expected IN ITEMS
            "position=0 outcome=complete"
            "position=1 outcome=incomplete diagnostic=[a-z_]+ playback=failed"
            "position=2 outcome=incomplete diagnostic=not_started_after_failure playback=not_started")
        if(NOT spec002_report MATCHES "${expected}")
            message(FATAL_ERROR "The failure did not stop the request (${expected}):\n${spec002_report}")
        endif()
    endforeach()
else()
    message(FATAL_ERROR "BROKEN_TAKE_FILE is required for the spec 002 failure case")
endif()

message(STATUS
    "Real Runtime evidence reopened; facts=129; pcm_blocks=${pcm_block_count}; initial=${initial_hash}; final=${final_hash}")
