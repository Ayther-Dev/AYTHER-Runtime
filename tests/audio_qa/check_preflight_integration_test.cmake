cmake_minimum_required(VERSION 3.25)

# Spec 002 (RF-1.1, RF-1.2, RF-1.3, RF-1.7, RF-2.2, RF-2.3; plan §5.2; contracts.md C5),
# without GPU: the supervisor validates every material with the real Runtime before
# admitting the request. A valid request without pack runs «Sin pack»; any validation
# error ends with 3 and creates nothing in the destination; a request id is known or
# conflicting only after it was admitted.
foreach(required IN ITEMS CHECK_EXE RUNTIME_EXE CORE_DLL NON_CORE_DLL ROM_FILE TAKE_FILE
                          BROKEN_TAKE_FILE FIXTURES TEST_ROOT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
    if(NOT "${required}" STREQUAL "TEST_ROOT" AND NOT EXISTS "${${required}}")
        message(FATAL_ERROR "${required} does not exist: ${${required}}")
    endif()
endforeach()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")
set(ENV{APPDATA} "${TEST_ROOT}/user-appdata")
set(REGISTRY "${FIXTURES}/public-synthetic-trust.toml")

function(check expected_code output_root)
    execute_process(
        COMMAND "${CHECK_EXE}" check --runtime "${RUNTIME_EXE}" --output "${output_root}"
                --language es ${ARGN}
        RESULT_VARIABLE code
        OUTPUT_VARIABLE output
        ERROR_VARIABLE errors
        TIMEOUT 60)
    set(report "${output}${errors}")
    if(NOT code EQUAL expected_code)
        message(FATAL_ERROR "check returned ${code}, expected ${expected_code}:\n${report}")
    endif()
    set(last_report "${report}" PARENT_SCOPE)
endfunction()

function(expect_in report)
    foreach(expected IN LISTS ARGN)
        if(NOT report MATCHES "${expected}")
            message(FATAL_ERROR "Missing '${expected}' in:\n${report}")
        endif()
    endforeach()
endfunction()

# A rejected request: exit 3, its issue in its field, and no destination.
function(rejected label expected_issue)
    set(destination "${TEST_ROOT}/rejected-${label}")
    check(3 "${destination}" ${ARGN})
    expect_in("${last_report}" "${expected_issue}")
    if(EXISTS "${destination}")
        message(FATAL_ERROR "[${label}] a rejected request created ${destination}")
    endif()
    message(STATUS "[preflight] ${label}: ${expected_issue}")
endfunction()

# 1. RF-1.1, RF-1.3: ROM, core and take are enough; the replay runs «Sin pack».
set(MAIN "${TEST_ROOT}/main")
check(0 "${MAIN}" --rom "${ROM_FILE}" --core "${CORE_DLL}" --take "${TAKE_FILE}"
      --request-id preflight-main)
expect_in("${last_report}"
    "audio_qa_effective: pack=none source=explicit"
    "audio_qa_effective: core=.* source=explicit"
    "audio_qa_status: request_accepted: preflight-main"
    "assignments=0"
    "audio_qa_summary: exit_code=0")

# 2. RF-2.3, RNF-5 (BR-066): the confirmed summary is durable and the same request returns
#    it as it is, with its exit code, without starting the Runtime again; another
#    selection with its id is a conflict.
file(GLOB summaries "${MAIN}/requests/*/request-summary.toml")
list(LENGTH summaries summary_count)
if(NOT summary_count EQUAL 1)
    message(FATAL_ERROR "The confirmed request has no durable summary: ${summaries}")
endif()
file(READ "${summaries}" summary_text)
expect_in("${summary_text}" "schema_version = 1" "schema_minor = 1" "confirmed = true"
          "pack = 'none'" "playback = 'natural_end'" "traversal = 'linear'"
          "evidence = 'complete'" "linear_complete = true")
file(GLOB runs_before "${MAIN}/runs/*")
check(0 "${MAIN}" --rom "${ROM_FILE}" --core "${CORE_DLL}" --take "${TAKE_FILE}"
      --request-id preflight-main)
expect_in("${last_report}" "request_known: preflight-main" "audio_qa_summary: exit_code=0"
          "playback=natural_end traversal=linear evidence=complete")
if(last_report MATCHES "audio_qa_replay:")
    message(FATAL_ERROR "A known request started the Runtime again:\n${last_report}")
endif()
file(GLOB runs_after "${MAIN}/runs/*")
if(NOT runs_before STREQUAL runs_after)
    message(FATAL_ERROR "A known request created runs: ${runs_after}")
endif()
check(3 "${MAIN}" --rom "${ROM_FILE}" --core "${CORE_DLL}" --take "${TAKE_FILE}"
      --pack "${FIXTURES}/public-synthetic.ay" --trust-registry "${REGISTRY}"
      --request-id preflight-main)
expect_in("${last_report}" "request_identity_conflict: preflight-main")

# 2b. RF-2.3 (BR-059): the identity includes the content. The same ROM path with other
#     bytes and the same request id is a conflict, found before anything runs.
file(COPY_FILE "${ROM_FILE}" "${TEST_ROOT}/same-path.aytest")
check(0 "${TEST_ROOT}/same-path" --rom "${TEST_ROOT}/same-path.aytest" --core "${CORE_DLL}"
      --take "${TAKE_FILE}" --request-id preflight-same-path)
file(WRITE "${TEST_ROOT}/same-path.aytest" "AYTHER-PUBLIC-QB")
check(3 "${TEST_ROOT}/same-path" --rom "${TEST_ROOT}/same-path.aytest" --core "${CORE_DLL}"
      --take "${TAKE_FILE}" --request-id preflight-same-path)
expect_in("${last_report}" "request_identity_conflict: preflight-same-path")
file(GLOB same_path_runs "${TEST_ROOT}/same-path/runs/*")
list(LENGTH same_path_runs same_path_run_count)
if(NOT same_path_run_count EQUAL 1)
    message(FATAL_ERROR "A conflicting request started a run: ${same_path_runs}")
endif()

# 3. RF-1.7, RF-2.2: each invalid selection stops the request before admission.
file(WRITE "${TEST_ROOT}/empty.aytest" "")
rejected("core-rejects-rom" "core_rejects_rom: --core"
    --rom "${TEST_ROOT}/empty.aytest" --core "${CORE_DLL}" --take "${TAKE_FILE}")
rejected("not-a-core" "core_invalid: --core"
    --rom "${ROM_FILE}" --core "${NON_CORE_DLL}" --take "${TAKE_FILE}")
rejected("signed-without-registry" "pack_trust_unverified: --trust-registry"
    --rom "${ROM_FILE}" --core "${CORE_DLL}" --take "${TAKE_FILE}"
    --pack "${FIXTURES}/public-synthetic.ay")
rejected("corrupt-asset" "pack_assets_unreadable: --pack"
    --rom "${ROM_FILE}" --core "${CORE_DLL}" --take "${TAKE_FILE}"
    --pack "${FIXTURES}/public-synthetic-corrupt-asset.ay" --trust-registry "${REGISTRY}")
rejected("missing-registry" "material_not_found: --trust-registry"
    --rom "${ROM_FILE}" --core "${CORE_DLL}" --take "${TAKE_FILE}"
    --pack "${FIXTURES}/public-synthetic.ay" --trust-registry "${TEST_ROOT}/missing.toml")
# D-10: a registry that exists but is not a registry is an error of --trust-registry, not
# pack_untrusted; a valid registry that revokes the key is pack_untrusted.
file(WRITE "${TEST_ROOT}/malformed-trust.toml" "version = [\n")
rejected("malformed-registry" "trust_registry_invalid: --trust-registry"
    --rom "${ROM_FILE}" --core "${CORE_DLL}" --take "${TAKE_FILE}"
    --pack "${FIXTURES}/public-synthetic.ay" --trust-registry "${TEST_ROOT}/malformed-trust.toml")
file(READ "${REGISTRY}" registry_text)
string(REPLACE "revoked = false" "revoked = true" revoked_text "${registry_text}")
file(WRITE "${TEST_ROOT}/revoked-trust.toml" "${revoked_text}")
rejected("revoked-registry" "pack_untrusted: --pack"
    --rom "${ROM_FILE}" --core "${CORE_DLL}" --take "${TAKE_FILE}"
    --pack "${FIXTURES}/public-synthetic.ay" --trust-registry "${TEST_ROOT}/revoked-trust.toml")
rejected("second-take-missing" "material_not_found: --take\\[1\\]"
    --rom "${ROM_FILE}" --core "${CORE_DLL}" --take "${TAKE_FILE}"
    --take "${TEST_ROOT}/missing.arp")
# BR-073 (contracts.md C1): visible replay needs protocol 1.1 with inspection_v1. Since
# BR-135 this Runtime offers both, so the rejection of a 1.0 Runtime is covered by
# audio_qa_runtime_protocol_v11 and the visible replay by the GPU integration.
rejected("take-not-arp" "take_invalid_magic: --take\\[0\\]"
    --rom "${ROM_FILE}" --core "${CORE_DLL}" --take "${ROM_FILE}")

# 4. RF-1.3: --pack-mode original keeps the pack unloaded, so it is not probed.
check(0 "${TEST_ROOT}/original" --rom "${ROM_FILE}" --core "${CORE_DLL}" --take "${TAKE_FILE}"
      --pack "${FIXTURES}/public-synthetic-corrupt-asset.ay" --pack-mode original
      --request-id preflight-original)
expect_in("${last_report}" "audio_qa_effective: pack_mode=original source=explicit"
          "audio_qa_summary: exit_code=0")

# 5. RF-2.1, RF-2.2 (BR-061): a valid pack that only replaces graphics replays to the
#    end and records zero audio assignments instead of failing.
check(0 "${TEST_ROOT}/visual" --rom "${ROM_FILE}" --core "${CORE_DLL}" --take "${TAKE_FILE}"
      --pack "${FIXTURES}/public-synthetic-visual.ay" --trust-registry "${REGISTRY}"
      --request-id preflight-visual)
expect_in("${last_report}" "assignments=0" "outcome=complete" "audio_qa_summary: exit_code=0")
if(last_report MATCHES "audio_assignment_catalog_empty")
    message(FATAL_ERROR "A visual-only pack was reported as an empty audio catalog")
endif()

# 6. D-1 (campaign 2026-10-04, BR-181; spec.md:17, RF-3.5): `--profile` without a pack is
#    the same condition as the run with the pack. It is accepted, shown and recorded as
#    requested with no effective profile, and the take never fails after admission.
check(0 "${TEST_ROOT}/profile-without-pack" --rom "${ROM_FILE}" --core "${CORE_DLL}"
      --take "${TAKE_FILE}" --profile full --request-id preflight-profile-without-pack)
expect_in("${last_report}"
    "audio_qa_effective: profile=full source=explicit"
    "audio_qa_effective: profile_effective=none source=generated"
    "playback=natural_end" "audio_qa_summary: exit_code=0")
file(GLOB profile_summaries "${TEST_ROOT}/profile-without-pack/requests/*/request-summary.toml")
file(READ "${profile_summaries}" profile_summary)
expect_in("${profile_summary}" "key = 'profile'" "value = 'full'" "key = 'profile_effective'"
          "value = 'none'")
if(last_report MATCHES "audio_profile_unavailable")
    message(FATAL_ERROR "A profile without pack failed after admission:\n${last_report}")
endif()
#    A pack that does not offer the requested profile is a mismatch: rejected before admission
#    in the field --profile (RF-2.2).
rejected("profile-not-in-pack" "profile_not_in_pack: --profile"
    --rom "${ROM_FILE}" --core "${CORE_DLL}" --take "${TAKE_FILE}"
    --pack "${FIXTURES}/public-synthetic.ay" --trust-registry "${REGISTRY}"
    --profile no-such-profile)

# 7. D-2 (campaign 2026-10-04, BR-181; RF-2.2): a take whose initial state is damaged is
#    rejected before admission in its own field, not when the Runtime restores it.
rejected("damaged-initial-state" "take_initial_state_invalid: --take\\[1\\]"
    --rom "${ROM_FILE}" --core "${CORE_DLL}" --take "${TAKE_FILE}" --take "${BROKEN_TAKE_FILE}")

# 8. D-13 (campaign 2026-10-05, BR-191; RF-2.2, RNF-3, RNF-7): the request identity holds
#    256 bytes. With 257 the request is rejected in the validation, in the field
#    --request-id, with its message in es and en, and nothing is created.
string(REPEAT "r" 256 ID_AT_LIMIT)
string(REPEAT "r" 257 ID_BEYOND)
check(0 "${TEST_ROOT}/request-id-256" --rom "${ROM_FILE}" --core "${CORE_DLL}"
      --take "${TAKE_FILE}" --request-id "${ID_AT_LIMIT}")
expect_in("${last_report}" "audio_qa_status: request_accepted: ${ID_AT_LIMIT}")
foreach(language IN ITEMS es en)
    set(destination "${TEST_ROOT}/rejected-request-id-257-${language}")
    execute_process(
        COMMAND "${CHECK_EXE}" check --runtime "${RUNTIME_EXE}" --output "${destination}"
                --language ${language} --rom "${ROM_FILE}" --core "${CORE_DLL}"
                --take "${TAKE_FILE}" --request-id "${ID_BEYOND}"
        RESULT_VARIABLE code
        OUTPUT_VARIABLE output
        ERROR_VARIABLE errors
        TIMEOUT 60)
    if(NOT code EQUAL 3)
        message(FATAL_ERROR "[request-id-257] check returned ${code}, expected 3:\n${output}${errors}")
    endif()
    if(language STREQUAL "es")
        set(expected_text "--request-id: El identificador de la solicitud supera 256 bytes.")
    else()
        set(expected_text "--request-id: The request identifier exceeds 256 bytes.")
    endif()
    expect_in("${output}${errors}" "audio_qa_error: request_id_too_long: --request-id"
              "audio_qa_message\\[${language}\\]: ${expected_text}")
    if(EXISTS "${destination}")
        message(FATAL_ERROR "[request-id-257] a rejected request created ${destination}")
    endif()
endforeach()
message(STATUS "[preflight] request-id-257: request_id_too_long: --request-id (es, en)")

# Nothing reached the user's Runtime data.
if(EXISTS "${TEST_ROOT}/user-appdata/Ayther")
    message(FATAL_ERROR "The preflight touched the user's Runtime data")
endif()
message(STATUS "[preflight] real materials validated before admission")
