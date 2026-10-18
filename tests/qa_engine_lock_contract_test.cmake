cmake_minimum_required(VERSION 3.25)

# Spec 002, BR-174 (RNF-8): the QA lock of the Runtime names the published `engine-vpx` artifact
# of AYTHER Engine v0.1.0-rc.18 by URL, with its SHA-256, the release checksums and the
# attestation of the release workflow, and no local path. The bootstrap requires the inspection
# headers of contracts C3 and C4 in the extracted prefix.
foreach(variable IN ITEMS LOCK BOOTSTRAP)
    if(NOT DEFINED ${variable} OR NOT EXISTS "${${variable}}")
        message(FATAL_ERROR "${variable} is required")
    endif()
endforeach()
file(READ "${LOCK}" lock)

set(tag "v0.1.0-rc.18")
set(root "ayther-engine-vpx-${tag}-windows-x86_64")
set(download "https://github.com/Ayther-Dev/AYTHER-Engine/releases/download/${tag}")

function(expect_json expected)
    string(JSON value ERROR_VARIABLE error GET "${lock}" ${ARGN})
    if(NOT error STREQUAL "NOTFOUND" OR NOT value STREQUAL expected)
        message(FATAL_ERROR "${ARGN}: '${value}' (${error}), expected '${expected}'")
    endif()
endfunction()

function(expect_absent)
    string(JSON value ERROR_VARIABLE error GET "${lock}" ${ARGN})
    if(error STREQUAL "NOTFOUND")
        message(FATAL_ERROR "${ARGN} must be absent from a release lock, found '${value}'")
    endif()
endfunction()

expect_json("qa-release" selection)
expect_json("${root}" package id)
expect_json("release" package kind)
expect_json("ON" package officialRelease)
expect_json("${tag}" release tag)
expect_json("${download}/CHECKSUMS.sha256" release checksumsUrl)
expect_json("21162e14cafc05149d23f5e36d38bdc089f928d05b740ebce67135ff50c9a6ba"
            release checksumsSha256)
expect_json("engine-vpx" artifact variant)
expect_json("${download}/${root}.zip" artifact url)
expect_json("${root}" artifact archiveRoot)
expect_json("9f62ba61b70a6dac3923a6f0dc8de99c72575f6c57f82bfe140b18adbd88eeca" artifact sha256)
expect_json("Ayther-Dev/AYTHER-Engine" attestation repository)
expect_json("Ayther-Dev/AYTHER-Engine/.github/workflows/release.yml" attestation signerWorkflow)
expect_json("refs/tags/${tag}" attestation sourceRef)
expect_absent(artifact relativePath)
expect_absent(artifact contentManifestRelativePath)

file(READ "${BOOTSTRAP}" bootstrap)
foreach(header IN ITEMS render_observer.hpp visual_state.hpp)
    if(NOT bootstrap MATCHES "include/ayther/engine/${header}")
        message(FATAL_ERROR "the QA bootstrap does not require ${header}")
    endif()
endforeach()
message(STATUS "the QA lock names the published ${root}")
