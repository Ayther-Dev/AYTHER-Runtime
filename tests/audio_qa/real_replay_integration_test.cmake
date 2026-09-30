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
        "durable_facts=123"
        "durable_pcm_blocks="
        "fact_integrity_complete=true"
        "relationships_reopened=true"
        "runtime_data_isolated=true"
        "status=replay_evidence_reopened"
        "outcome=complete"
        "diagnostic=replay_evidence_complete"
        "audible_restart_observed=false"
        "audible_overlap_observed=false")
    string(FIND "${report}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Real replay omitted ${expected}:\n${report}")
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
if(NOT failed_result EQUAL 2 OR
   NOT failed_report MATCHES "diagnostic=audio_assignment_catalog_empty" OR
   failed_report MATCHES "runtime_replay_input_mismatch")
    message(FATAL_ERROR
        "Failed replay did not preserve its Runtime diagnostic: result=${failed_result}\n"
        "${failed_report}")
endif()

message(STATUS
    "Real Runtime evidence reopened; facts=123; pcm_blocks=${pcm_block_count}; initial=${initial_hash}; final=${final_hash}")
