cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED TOOLCHAIN_MODULE)
    message(FATAL_ERROR "TOOLCHAIN_MODULE is required")
endif()
include("${TOOLCHAIN_MODULE}")

function(expect_compatible label)
    _ayther_evaluate_windows_engine_toolchain(_ok _message ${ARGN})
    if(NOT _ok)
        message(FATAL_ERROR "${label}: expected compatible, got:\n${_message}")
    endif()
endfunction()

function(expect_rejected label expected_pattern)
    _ayther_evaluate_windows_engine_toolchain(_ok _message ${ARGN})
    if(_ok)
        message(FATAL_ERROR "${label}: expected rejection")
    endif()
    if(NOT _message MATCHES "${expected_pattern}")
        message(FATAL_ERROR
            "${label}: diagnostic did not match '${expected_pattern}':\n"
            "${_message}")
    endif()
    foreach(_required_pattern IN ITEMS
            "Detected compiler frontend:"
            "Detected MSVC toolset:"
            "Minimum required toolset: MSVC v145 14.51"
            "Engine artifact: AYTHER Engine v0.1.0-rc.6"
            "Solution:")
        if(NOT _message MATCHES "${_required_pattern}")
            message(FATAL_ERROR
                "${label}: diagnostic omitted '${_required_pattern}':\n"
                "${_message}")
        endif()
    endforeach()
endfunction()

set(_common
    ENGINE_RELEASE "v0.1.0-rc.6"
    MIN_TOOLSET_VERSION "14.51"
)

expect_compatible("MSVC v145 14.51"
    COMPILER_ID "MSVC"
    COMPILER_VERSION "19.51.36256.0"
    TOOLSET_VERSION "14.51.36231"
    TOOLSET_EVIDENCE "test"
    ${_common}
)

expect_compatible("clang-cl over v145 14.51"
    COMPILER_ID "Clang"
    COMPILER_VERSION "20.1.8"
    SIMULATE_ID "MSVC"
    FRONTEND_VARIANT "MSVC"
    TOOLSET_VERSION "14.51.36231"
    TOOLSET_EVIDENCE "test"
    ${_common}
)

expect_rejected("MinGW/GNU" "MinGW/GNU"
    COMPILER_ID "GNU"
    COMPILER_VERSION "15.2.0"
    TOOLSET_VERSION "14.51.36231"
    TOOLSET_EVIDENCE "test"
    ${_common}
)

expect_rejected("old MSVC toolset" "14.43.34808"
    COMPILER_ID "MSVC"
    COMPILER_VERSION "19.43.34810.0"
    TOOLSET_VERSION "14.43.34808"
    TOOLSET_EVIDENCE "test"
    ${_common}
)

expect_rejected("unknown clang-cl toolset" "not detected"
    COMPILER_ID "Clang"
    COMPILER_VERSION "20.1.8"
    SIMULATE_ID "MSVC"
    FRONTEND_VARIANT "MSVC"
    ${_common}
)

message(STATUS "Windows Engine toolchain contract cases passed")
