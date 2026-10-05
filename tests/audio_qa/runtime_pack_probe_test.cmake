cmake_minimum_required(VERSION 3.25)

# Spec 002 (contracts.md C5, «Sondeo de pack del Runtime»; RF-2.2): the QA Runtime
# probes a pack without starting a game. A valid signed pack with its registry can be
# used (0); the same pack without a registry is unverified, and a pack with an asset
# that does not decode lists it; both end with 66.
foreach(required IN ITEMS RUNTIME_EXE FIXTURES)
    if(NOT DEFINED ${required} OR NOT EXISTS "${${required}}")
        message(FATAL_ERROR "${required} is required and must exist")
    endif()
endforeach()

set(REGISTRY "${FIXTURES}/public-synthetic-trust.toml")

function(probe label expected_code out_json)
    execute_process(
        COMMAND "${RUNTIME_EXE}" --probe-pack ${ARGN}
        RESULT_VARIABLE code
        OUTPUT_VARIABLE output
        ERROR_VARIABLE errors
        TIMEOUT 30)
    if(NOT code EQUAL expected_code)
        message(FATAL_ERROR
            "[${label}] exit ${code}, expected ${expected_code}\n${output}\n${errors}")
    endif()
    string(REGEX MATCHALL "AYTHER_PACK_PROBE [^\n]*" lines "${output}")
    list(LENGTH lines line_count)
    if(NOT line_count EQUAL 1)
        message(FATAL_ERROR "[${label}] expected one AYTHER_PACK_PROBE line:\n${output}")
    endif()
    string(REPLACE "AYTHER_PACK_PROBE " "" json "${lines}")
    string(JSON schema ERROR_VARIABLE json_error GET "${json}" schema)
    if(NOT json_error STREQUAL "NOTFOUND" OR NOT schema STREQUAL "1.0")
        message(FATAL_ERROR "[${label}] the probe line is not schema 1.0 JSON: ${json}")
    endif()
    set(${out_json} "${json}" PARENT_SCOPE)
    message(STATUS "[pack probe] ${label}: ${json}")
endfunction()

function(expect_field label json expected)
    string(JSON value ERROR_VARIABLE error GET "${json}" ${ARGN})
    if(NOT error STREQUAL "NOTFOUND" OR NOT "${value}" STREQUAL "${expected}")
        message(FATAL_ERROR
            "[${label}] ${ARGN} is '${value}' (${error}), expected '${expected}'\n${json}")
    endif()
endfunction()

function(expect_length label json expected)
    string(JSON value ERROR_VARIABLE error LENGTH "${json}" ${ARGN})
    if(NOT error STREQUAL "NOTFOUND" OR NOT "${value}" STREQUAL "${expected}")
        message(FATAL_ERROR
            "[${label}] ${ARGN} has ${value} elements (${error}), expected ${expected}\n${json}")
    endif()
endfunction()

# 1. The valid signed pack with its trust registry.
probe("valid" 0 valid "${FIXTURES}/public-synthetic.ay" --trust-registry "${REGISTRY}")
expect_field("valid" "${valid}" "ON" opened)
expect_field("valid" "${valid}" "valid" signature)
expect_field("valid" "${valid}" "trusted" trust)
expect_field("valid" "${valid}" "0" catalog poses)
expect_field("valid" "${valid}" "1" catalog audio_events)
expect_length("valid" "${valid}" "0" unreadable_assets)
expect_field("valid" "${valid}" "ayther-public-synthetic-v1" game_id)

probe("valid with poses" 0 poses "${FIXTURES}/public-synthetic-poses.ay"
      --trust-registry "${REGISTRY}")
expect_field("valid with poses" "${poses}" "1" catalog poses)
expect_length("valid with poses" "${poses}" "0" unreadable_assets)

# 2. The same signed pack without a trust registry.
probe("signed without registry" 66 unverified "${FIXTURES}/public-synthetic.ay")
expect_field("signed without registry" "${unverified}" "unverified" trust)
expect_field("signed without registry" "${unverified}" "unverified" signature)
expect_field("signed without registry" "${unverified}" "pack_trust_unverified" reason)

# 3. A signed pack whose pose image is listed and signed but does not decode.
probe("corrupt asset" 66 corrupt "${FIXTURES}/public-synthetic-corrupt-asset.ay"
      --trust-registry "${REGISTRY}")
expect_field("corrupt asset" "${corrupt}" "ON" opened)
expect_field("corrupt asset" "${corrupt}" "trusted" trust)
expect_length("corrupt asset" "${corrupt}" "1" unreadable_assets)
expect_field("corrupt asset" "${corrupt}" "bf4a1789ea6a189f925e9d211f8289f2"
             unreadable_assets 0)
expect_field("corrupt asset" "${corrupt}" "pack_assets_unreadable" reason)

# D-10 (campaign 2026-10-04): a registry that cannot be used is a configuration error of the
# registry (78, as for a launch), never a trust failure of the pack. A quoted path, as Windows
# "Copy as path" pastes it, names no file.
file(MAKE_DIRECTORY "${WORK}")
probe("quoted registry" 78 quoted "${FIXTURES}/public-synthetic.ay"
      --trust-registry "\"${REGISTRY}\"")
expect_field("quoted registry" "${quoted}" "trust_registry_invalid" reason)
expect_field("quoted registry" "${quoted}" "unknown" trust)
probe("missing registry" 78 missing "${FIXTURES}/public-synthetic.ay"
      --trust-registry "${WORK}/no-such-trust.toml")
expect_field("missing registry" "${missing}" "trust_registry_invalid" reason)
file(WRITE "${WORK}/malformed-trust.toml" "version = [\n")
probe("malformed registry" 78 malformed "${FIXTURES}/public-synthetic.ay"
      --trust-registry "${WORK}/malformed-trust.toml")
expect_field("malformed registry" "${malformed}" "trust_registry_invalid" reason)
# A valid registry whose key is revoked: the Engine refuses trust, and only then is it
# pack_untrusted.
file(READ "${REGISTRY}" registry_text)
string(REPLACE "revoked = false" "revoked = true" revoked_text "${registry_text}")
file(WRITE "${WORK}/revoked-trust.toml" "${revoked_text}")
probe("revoked key" 66 revoked "${FIXTURES}/public-synthetic.ay"
      --trust-registry "${WORK}/revoked-trust.toml")
expect_field("revoked key" "${revoked}" "untrusted" trust)
expect_field("revoked key" "${revoked}" "pack_untrusted" reason)

message(STATUS "[pack probe] 4 packs and 4 registries verified")
