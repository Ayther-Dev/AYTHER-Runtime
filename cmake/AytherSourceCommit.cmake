# Spec 002, contracts.md C5 (RF-1.8, D-3 of the 2026-10-04 campaign): writes the commit of
# the sources being built into a header, so that `ayther_audio_qa options --format toml`
# publishes it like the reference inventory. Run at build time (cmake -P), so that a new
# commit reaches the next build without reconfiguring; the header is rewritten only when its
# content changes.
#
#   -DSOURCE_DIR=<checkout> -DOUTPUT=<header> [-DCOMMIT=<40 hex digits>]
#
# An explicit COMMIT wins (a build from an exported archive); otherwise `git rev-parse HEAD`
# of SOURCE_DIR. Without either the commit is empty: nothing is invented.
if(NOT DEFINED SOURCE_DIR OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "AytherSourceCommit.cmake needs SOURCE_DIR and OUTPUT")
endif()

set(_ayther_commit "")
if(DEFINED COMMIT AND NOT COMMIT STREQUAL "")
    set(_ayther_commit "${COMMIT}")
else()
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" rev-parse HEAD
            OUTPUT_VARIABLE _ayther_commit
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE _ayther_git_result)
        if(NOT _ayther_git_result EQUAL 0)
            set(_ayther_commit "")
        endif()
    endif()
endif()
string(TOLOWER "${_ayther_commit}" _ayther_commit)
string(LENGTH "${_ayther_commit}" _ayther_length)
if(NOT _ayther_length EQUAL 40 OR NOT _ayther_commit MATCHES "^[0-9a-f]+$")
    set(_ayther_commit "")
endif()

set(_ayther_content "#pragma once

#include <string_view>

namespace ayther::runtime {

// The commit of the sources of this build; empty when it is not known.
inline constexpr std::string_view source_commit{\"${_ayther_commit}\"};

}  // namespace ayther::runtime
")
set(_ayther_current "")
if(EXISTS "${OUTPUT}")
    file(READ "${OUTPUT}" _ayther_current)
endif()
if(NOT _ayther_current STREQUAL _ayther_content)
    file(WRITE "${OUTPUT}" "${_ayther_content}")
endif()
