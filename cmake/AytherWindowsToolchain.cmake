include_guard(GLOBAL)

include(CMakeParseArguments)

function(_ayther_detect_msvc_toolset out_version out_evidence)
    set(_version "")
    set(_evidence "")

    if(CMAKE_VS_PLATFORM_TOOLSET_VERSION)
        string(REGEX MATCH "[0-9]+\\.[0-9]+(\\.[0-9]+)*" _match
            "${CMAKE_VS_PLATFORM_TOOLSET_VERSION}")
        if(_match)
            set(_version "${_match}")
            set(_evidence "CMAKE_VS_PLATFORM_TOOLSET_VERSION")
        endif()
    endif()

    if(NOT _version AND DEFINED ENV{VCToolsVersion})
        string(REGEX MATCH "[0-9]+\\.[0-9]+(\\.[0-9]+)*" _match
            "$ENV{VCToolsVersion}")
        if(_match)
            set(_version "${_match}")
            set(_evidence "VCToolsVersion")
        endif()
    endif()

    if(NOT _version)
        foreach(_path IN ITEMS
                "$ENV{VCToolsInstallDir}"
                "${CMAKE_CXX_COMPILER}"
                "${CMAKE_LINKER}"
                "${CMAKE_AR}")
            file(TO_CMAKE_PATH "${_path}" _normalized_path)
            if(_normalized_path MATCHES
                    "/VC/Tools/MSVC/([0-9]+\\.[0-9]+(\\.[0-9]+)*)/")
                set(_version "${CMAKE_MATCH_1}")
                set(_evidence "${_normalized_path}")
                break()
            endif()
        endforeach()
    endif()

    # cl.exe's 19.xx frontend version maps to the 14.xx MSVC toolset. This is
    # a final fallback for non-standard installations whose paths do not carry
    # the toolset version. clang-cl cannot use this fallback because its own
    # frontend version is intentionally independent from the MSVC ABI toolset.
    if(NOT _version AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND
            CMAKE_CXX_COMPILER_VERSION MATCHES "^19\\.([0-9]+)")
        set(_version "14.${CMAKE_MATCH_1}")
        set(_evidence
            "derived from MSVC frontend ${CMAKE_CXX_COMPILER_VERSION}")
    endif()

    set(${out_version} "${_version}" PARENT_SCOPE)
    set(${out_evidence} "${_evidence}" PARENT_SCOPE)
endfunction()

function(_ayther_evaluate_windows_engine_toolchain out_ok out_message)
    set(_one_value_args
        COMPILER_ID
        COMPILER_VERSION
        SIMULATE_ID
        FRONTEND_VARIANT
        TOOLSET_VERSION
        TOOLSET_EVIDENCE
        ENGINE_RELEASE
        MIN_TOOLSET_VERSION
    )
    cmake_parse_arguments(ARG "" "${_one_value_args}" "" ${ARGN})

    if(ARG_COMPILER_VERSION)
        set(_frontend "${ARG_COMPILER_ID} ${ARG_COMPILER_VERSION}")
    else()
        set(_frontend "${ARG_COMPILER_ID}")
    endif()

    set(_uses_msvc_abi FALSE)
    if(ARG_COMPILER_ID STREQUAL "MSVC")
        set(_uses_msvc_abi TRUE)
        string(APPEND _frontend " (cl/MSVC ABI)")
    elseif(ARG_COMPILER_ID MATCHES "Clang" AND
            (ARG_SIMULATE_ID STREQUAL "MSVC" OR
             ARG_FRONTEND_VARIANT STREQUAL "MSVC"))
        set(_uses_msvc_abi TRUE)
        string(APPEND _frontend " (clang-cl/MSVC ABI)")
    endif()

    if(ARG_TOOLSET_VERSION)
        set(_toolset "${ARG_TOOLSET_VERSION}")
        if(ARG_TOOLSET_EVIDENCE)
            string(APPEND _toolset " (${ARG_TOOLSET_EVIDENCE})")
        endif()
    else()
        set(_toolset "not detected")
    endif()

    set(_contract
        "Detected compiler frontend: ${_frontend}\n"
        "Detected MSVC toolset: ${_toolset}\n"
        "Minimum required toolset: MSVC v145 ${ARG_MIN_TOOLSET_VERSION}\n"
        "Engine artifact: AYTHER Engine ${ARG_ENGINE_RELEASE}\n"
        "Solution: run CMake from a Visual Studio 2026 developer environment "
        "with MSVC v145 ${ARG_MIN_TOOLSET_VERSION} or newer, or configure "
        "clang-cl against that "
        "same toolset. Otherwise use or rebuild an Engine artifact matching "
        "the selected Windows ABI/toolset.")
    string(JOIN "" _contract ${_contract})

    if(NOT _uses_msvc_abi)
        set(_reason
            "The current Windows Engine artifact does not support the "
            "${ARG_COMPILER_ID} frontend. MinGW/GNU and other non-MSVC ABIs "
            "are incompatible.")
        string(JOIN "" _reason ${_reason})
        set(${out_ok} FALSE PARENT_SCOPE)
        set(${out_message} "${_reason}\n${_contract}" PARENT_SCOPE)
        return()
    endif()

    if(NOT ARG_TOOLSET_VERSION)
        set(_reason
            "The MSVC ABI frontend is supported, but CMake could not identify "
            "the Visual C++ toolset used for headers and linking.")
        string(JOIN "" _reason ${_reason})
        set(${out_ok} FALSE PARENT_SCOPE)
        set(${out_message} "${_reason}\n${_contract}" PARENT_SCOPE)
        return()
    endif()

    if(ARG_TOOLSET_VERSION VERSION_LESS ARG_MIN_TOOLSET_VERSION)
        set(_reason
            "The detected Visual C++ toolset is older than the toolset used "
            "by the published Windows Engine archive.")
        string(JOIN "" _reason ${_reason})
        set(${out_ok} FALSE PARENT_SCOPE)
        set(${out_message} "${_reason}\n${_contract}" PARENT_SCOPE)
        return()
    endif()

    set(${out_ok} TRUE PARENT_SCOPE)
    set(${out_message} "${_contract}" PARENT_SCOPE)
endfunction()

function(_ayther_assert_current_windows_engine_toolchain
        out_diagnostic out_toolset_version)
    set(_one_value_args ENGINE_RELEASE MIN_TOOLSET_VERSION)
    cmake_parse_arguments(ARG "" "${_one_value_args}" "" ${ARGN})

    foreach(_required IN ITEMS ENGINE_RELEASE MIN_TOOLSET_VERSION)
        if(NOT ARG_${_required})
            message(FATAL_ERROR
                "Windows toolchain validation requires ${_required}")
        endif()
    endforeach()

    _ayther_detect_msvc_toolset(_toolset_version _toolset_evidence)
    _ayther_evaluate_windows_engine_toolchain(_compatible _diagnostic
        COMPILER_ID "${CMAKE_CXX_COMPILER_ID}"
        COMPILER_VERSION "${CMAKE_CXX_COMPILER_VERSION}"
        SIMULATE_ID "${CMAKE_CXX_SIMULATE_ID}"
        FRONTEND_VARIANT "${CMAKE_CXX_COMPILER_FRONTEND_VARIANT}"
        TOOLSET_VERSION "${_toolset_version}"
        TOOLSET_EVIDENCE "${_toolset_evidence}"
        ENGINE_RELEASE "${ARG_ENGINE_RELEASE}"
        MIN_TOOLSET_VERSION "${ARG_MIN_TOOLSET_VERSION}"
    )
    if(NOT _compatible)
        message(FATAL_ERROR
            "AYTHER Windows toolchain contract failed.\n${_diagnostic}")
    endif()

    set(${out_diagnostic} "${_diagnostic}" PARENT_SCOPE)
    set(${out_toolset_version} "${_toolset_version}" PARENT_SCOPE)
endfunction()

function(ayther_validate_windows_engine_toolchain)
    _ayther_assert_current_windows_engine_toolchain(
        _diagnostic _toolset_version ${ARGN})

    message(STATUS
        "AYTHER Windows toolchain contract: ${CMAKE_CXX_COMPILER_ID} "
        "${CMAKE_CXX_COMPILER_VERSION}; MSVC toolset ${_toolset_version}; "
        "frontend and ABI requirements passed")
endfunction()

function(ayther_verify_windows_engine_link)
    set(_one_value_args ENGINE_RELEASE MIN_TOOLSET_VERSION TARGET)
    cmake_parse_arguments(ARG "" "${_one_value_args}" "" ${ARGN})

    foreach(_required IN ITEMS ENGINE_RELEASE MIN_TOOLSET_VERSION TARGET)
        if(NOT ARG_${_required})
            message(FATAL_ERROR
                "ayther_verify_windows_engine_link requires ${_required}")
        endif()
    endforeach()
    if(NOT TARGET "${ARG_TARGET}")
        message(FATAL_ERROR
            "Windows toolchain validation target does not exist: ${ARG_TARGET}")
    endif()

    _ayther_assert_current_windows_engine_toolchain(
        _diagnostic _toolset_version
        ENGINE_RELEASE "${ARG_ENGINE_RELEASE}"
        MIN_TOOLSET_VERSION "${ARG_MIN_TOOLSET_VERSION}"
    )

    set(_probe_source_dir
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/probes/ayther_engine_link")
    set(_probe_binary_dir
        "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/ayther-engine-link-probe")
    set(_probe_cmake_flags
        "-DAyther_DIR:PATH=${Ayther_DIR}"
        "-DCMAKE_BUILD_TYPE:STRING=Release"
    )
    if(CMAKE_TOOLCHAIN_FILE)
        list(APPEND _probe_cmake_flags
            "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}")
    endif()
    if(VCPKG_INSTALLED_DIR)
        list(APPEND _probe_cmake_flags
            "-DVCPKG_INSTALLED_DIR:PATH=${VCPKG_INSTALLED_DIR}")
    endif()
    if(VCPKG_TARGET_TRIPLET)
        list(APPEND _probe_cmake_flags
            "-DVCPKG_TARGET_TRIPLET:STRING=${VCPKG_TARGET_TRIPLET}")
    endif()
    if(CMAKE_PREFIX_PATH)
        string(REPLACE ";" "\\;" _probe_prefix_path "${CMAKE_PREFIX_PATH}")
        list(APPEND _probe_cmake_flags
            "-DCMAKE_PREFIX_PATH:STRING=${_probe_prefix_path}")
    endif()

    try_compile(
        AYTHER_WINDOWS_ENGINE_LINK_COMPATIBLE
        "${_probe_binary_dir}"
        "${_probe_source_dir}"
        AytherEngineLinkProbe
        ayther_engine_link_probe
        CMAKE_FLAGS ${_probe_cmake_flags}
        OUTPUT_VARIABLE _probe_output
    )

    if(NOT AYTHER_WINDOWS_ENGINE_LINK_COMPATIBLE)
        message(FATAL_ERROR
            "AYTHER Windows toolchain contract link probe failed.\n"
            "${_diagnostic}\n"
            "The compiler accepted Engine headers, but an executable could "
            "not link with ${ARG_TARGET}. Linker output follows:\n"
            "${_probe_output}")
    endif()

    message(STATUS
        "AYTHER Windows Engine link contract: ${ARG_ENGINE_RELEASE}; "
        "${ARG_TARGET} link probe passed")
endfunction()
