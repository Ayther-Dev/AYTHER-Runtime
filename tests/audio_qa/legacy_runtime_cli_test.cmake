cmake_minimum_required(VERSION 3.25)

foreach(required IN ITEMS CHECK_EXE LEGACY_RUNTIME_EXE CORE_DLL ROM_FILE
                          PACK_FILE TRUST_REGISTRY TAKE_FILE TEST_ROOT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
    if(NOT "${required}" STREQUAL "TEST_ROOT" AND NOT EXISTS "${${required}}")
        message(FATAL_ERROR "${required} does not exist: ${${required}}")
    endif()
endforeach()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")
foreach(path_name IN ITEMS CORE_DLL ROM_FILE PACK_FILE)
    file(TO_CMAKE_PATH "${${path_name}}" ${path_name}_TOML)
endforeach()
set(MANIFEST "${TEST_ROOT}/launch.toml")
file(WRITE "${MANIFEST}"
    "format = 1\n"
    "id = \"qa173-legacy-runtime\"\n"
    "created = \"2026-09-29T00:00:00Z\"\n"
    "play_version = \"0.1.0\"\n"
    "rom = \"${ROM_FILE_TOML}\"\n"
    "core = \"${CORE_DLL_TOML}\"\n"
    "pack = \"${PACK_FILE_TOML}\"\n")

# Spec 002 (RF-1.1): the ROM is explicit and the campaign reference is optional.
execute_process(
    COMMAND "${CHECK_EXE}" check
        --runtime "${LEGACY_RUNTIME_EXE}"
        --rom "${ROM_FILE}"
        --play-manifest "${MANIFEST}"
        --pack "${PACK_FILE}"
        --trust-registry "${TRUST_REGISTRY}"
        --take "${TAKE_FILE}"
        --output "${TEST_ROOT}/evidence"
        --request-id "qa173-legacy-request"
        --language es
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors
    TIMEOUT 30)

# Spec 002 (RF-2.2, contracts.md C5): a Runtime without the ROM probe is found before
# admission and the request ends with 3.
if(NOT result EQUAL 3)
    message(FATAL_ERROR
        "Legacy Runtime check returned ${result}, expected a preflight rejection (3)\n"
        "stdout:\n${output}\nstderr:\n${errors}")
endif()
set(report "${output}${errors}")
if(NOT report MATCHES "audio_qa_error: (runtime_rom_probe_unsupported|runtime_probe_failed): --runtime")
    message(FATAL_ERROR "Legacy check omitted the Runtime probe rejection:\n${report}")
endif()
foreach(forbidden IN ITEMS
        "audio_qa_replay:"
        "status=replay_evidence_reopened"
        "diagnostic=replay_complete_evaluation_pending"
        "diagnostic=replay_evidence_complete")
    string(FIND "${report}" "${forbidden}" found)
    if(NOT found EQUAL -1)
        message(FATAL_ERROR "Legacy check falsely credited ${forbidden}:\n${report}")
    endif()
endforeach()
file(GLOB_RECURSE replay_artifacts
    "${TEST_ROOT}/evidence/runs/*.aqf"
    "${TEST_ROOT}/evidence/runs/*.aqp")
if(replay_artifacts)
    message(FATAL_ERROR
        "Legacy Runtime produced replay artifacts: ${replay_artifacts}")
endif()

message(STATUS
    "Legacy Runtime rejected before admission by the Runtime probe")
