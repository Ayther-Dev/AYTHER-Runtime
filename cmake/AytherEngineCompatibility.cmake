include_guard(GLOBAL)

include(CMakeParseArguments)

function(_ayther_evaluate_engine_package_contract out_ok out_message)
    set(_one_value_args
        PACKAGE_VERSION
        MIN_VERSION
        MAX_VERSION_EXCLUSIVE
        SYSTEM_NAME
        SYSTEM_PROCESSOR
        POINTER_SIZE
        COMPILER_ID
        COMPILER_VERSION
        SIMULATE_ID
    )
    cmake_parse_arguments(ARG "" "${_one_value_args}" "" ${ARGN})

    set(_requirement
        "Required AYTHER Engine package: >=${ARG_MIN_VERSION} and "
        "<${ARG_MAX_VERSION_EXCLUSIVE}; x86-64; "
        "Windows MSVC ABI v145 or Ubuntu 24.04 Clang 18 + libstdc++.")
    string(JOIN "" _requirement ${_requirement})
    set(_found
        "Found package=${ARG_PACKAGE_VERSION}; platform=${ARG_SYSTEM_NAME}; "
        "processor=${ARG_SYSTEM_PROCESSOR}; pointer_size=${ARG_POINTER_SIZE}; "
        "compiler=${ARG_COMPILER_ID} ${ARG_COMPILER_VERSION}; "
        "simulate=${ARG_SIMULATE_ID}.")
    string(JOIN "" _found ${_found})

    macro(_ayther_reject reason)
        set(${out_ok} FALSE PARENT_SCOPE)
        set(${out_message}
            "${reason}\n${_found}\n${_requirement}\n"
            "Use the locked v0.1.0-rc.6 archive for this platform, or rebuild "
            "both Engine and Runtime with one documented ABI."
            PARENT_SCOPE)
        return()
    endmacro()

    if(NOT ARG_PACKAGE_VERSION)
        _ayther_reject("The Engine package did not publish a version.")
    endif()
    if(ARG_PACKAGE_VERSION VERSION_LESS ARG_MIN_VERSION OR
       NOT ARG_PACKAGE_VERSION VERSION_LESS ARG_MAX_VERSION_EXCLUSIVE)
        _ayther_reject("The Engine package version is outside the supported range.")
    endif()
    if(NOT ARG_POINTER_SIZE STREQUAL "8" OR
       NOT ARG_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|x64)$")
        _ayther_reject("The Engine package architecture is incompatible.")
    endif()

    if(ARG_SYSTEM_NAME STREQUAL "Windows")
        if(NOT ARG_COMPILER_ID STREQUAL "MSVC" AND
           NOT (ARG_COMPILER_ID MATCHES "Clang" AND
                ARG_SIMULATE_ID STREQUAL "MSVC"))
            _ayther_reject("Windows requires an MSVC-ABI compiler frontend.")
        endif()
    elseif(ARG_SYSTEM_NAME STREQUAL "Linux")
        if(NOT ARG_COMPILER_ID MATCHES "Clang" OR
           ARG_COMPILER_VERSION VERSION_LESS "18" OR
           NOT ARG_COMPILER_VERSION VERSION_LESS "19")
            _ayther_reject("Linux support is limited to the Clang 18 ABI baseline.")
        endif()
    else()
        _ayther_reject("The selected operating system has no supported Engine artifact.")
    endif()

    set(${out_ok} TRUE PARENT_SCOPE)
    set(${out_message} "${_found}\n${_requirement}" PARENT_SCOPE)
endfunction()

function(ayther_validate_engine_package_contract)
    _ayther_evaluate_engine_package_contract(_compatible _diagnostic ${ARGN})
    if(NOT _compatible)
        message(FATAL_ERROR "AYTHER Engine package contract failed.\n${_diagnostic}")
    endif()
    message(STATUS "AYTHER Engine package contract: ${_diagnostic}")
endfunction()
