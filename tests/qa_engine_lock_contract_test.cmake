cmake_minimum_required(VERSION 3.25)

# Spec 002, BR-174 (RNF-8): the QA lock of the Runtime names the published `engine-vpx` artifact
# of AYTHER Engine v0.1.0-rc.22 by URL, with its SHA-256, the release checksums and the
# attestation of the release workflow, and no local path. The bootstrap requires the inspection
# headers of contracts C3 and C4 in the extracted prefix.
foreach(variable IN ITEMS LOCK BOOTSTRAP)
    if(NOT DEFINED ${variable} OR NOT EXISTS "${${variable}}")
        message(FATAL_ERROR "${variable} is required")
    endif()
endforeach()
file(READ "${LOCK}" lock)

set(tag "v0.1.0-rc.22")
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
expect_json("56c4d41d0d5fdf7a57f85bdd51181a3de6b835d9590a9cd2e56ffed9b6db9b9d"
            release checksumsSha256)
expect_json("engine-vpx" artifact variant)
expect_json("${download}/${root}.zip" artifact url)
expect_json("${root}" artifact archiveRoot)
expect_json("2a2c11be526da6a6e189b24ff17d18e0bcb1c16ce5abf6513d0ec920c93eb0f1" artifact sha256)
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
