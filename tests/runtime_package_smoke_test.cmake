if(NOT DEFINED RUNTIME_EXE OR NOT EXISTS "${RUNTIME_EXE}")
    message(FATAL_ERROR "Runtime executable is missing: ${RUNTIME_EXE}")
endif()
if(NOT DEFINED BUILD_DIR OR NOT IS_DIRECTORY "${BUILD_DIR}")
    message(FATAL_ERROR "Runtime build directory is missing: ${BUILD_DIR}")
endif()

if(NOT DEFINED SMOKE_DIR OR SMOKE_DIR STREQUAL "")
    set(SMOKE_DIR "${BUILD_DIR}/runtime-package-smoke")
endif()
file(REMOVE_RECURSE "${SMOKE_DIR}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}"
            --config "${INSTALL_CONFIG}"
            --prefix "${SMOKE_DIR}"
    RESULT_VARIABLE install_result
    OUTPUT_VARIABLE install_stdout
    ERROR_VARIABLE install_stderr
)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR
        "Runtime install failed (exit=${install_result}).\n"
        "stdout:\n${install_stdout}\nstderr:\n${install_stderr}")
endif()

get_filename_component(runtime_name "${RUNTIME_EXE}" NAME)
set(package_bin "${SMOKE_DIR}/bin")
set(packaged_runtime "${package_bin}/${runtime_name}")
if(NOT EXISTS "${packaged_runtime}")
    message(FATAL_ERROR
        "Installed Runtime executable is missing: ${packaged_runtime}")
endif()
if(WIN32)
    file(GLOB toml_runtime_dlls "${package_bin}/tomlplusplus*.dll")
    if(NOT toml_runtime_dlls)
        message(FATAL_ERROR
            "tomlplusplus runtime DLL was not installed next to ${packaged_runtime}")
    endif()
endif()
if(NOT EXISTS "${package_bin}/shaders/postprocess.vert.spv" OR
   NOT EXISTS "${package_bin}/shaders/postprocess.frag.spv")
    message(FATAL_ERROR "Installed Runtime shaders are incomplete")
endif()
if(NOT EXISTS "${SMOKE_DIR}/share/licenses/ayther-runtime/LICENSE")
    message(FATAL_ERROR "Installed Runtime license is missing")
endif()
set(package_metadata
    "${SMOKE_DIR}/share/ayther-runtime/ayther-runtime-package.json")
if(NOT EXISTS "${package_metadata}")
    message(FATAL_ERROR "Installed Runtime package metadata is missing")
endif()
file(READ "${package_metadata}" metadata)
foreach(field IN ITEMS layout_version runtime_version protocol_version
                       engine_release engine_min_version
                       engine_max_version_exclusive)
    string(JSON field_type ERROR_VARIABLE field_error TYPE "${metadata}" "${field}")
    if(NOT field_error STREQUAL "NOTFOUND")
        message(FATAL_ERROR "Package metadata omits '${field}': ${field_error}")
    endif()
endforeach()

set(clean_working_directory "${SMOKE_DIR}-cwd")
file(REMOVE_RECURSE "${clean_working_directory}")
file(MAKE_DIRECTORY "${clean_working_directory}")

# An intentional CLI error proves the packaged process reached main(). A
# missing transitive DLL would fail in the Windows loader before producing the
# stable EX_USAGE-compatible status.
execute_process(
    COMMAND "${packaged_runtime}" --definitely-invalid-option
    WORKING_DIRECTORY "${clean_working_directory}"
    RESULT_VARIABLE runtime_result
    OUTPUT_VARIABLE runtime_stdout
    ERROR_VARIABLE runtime_stderr
)
if(NOT runtime_result EQUAL 64)
    message(FATAL_ERROR
        "Packaged Runtime did not reach CLI parsing (exit=${runtime_result}).\n"
        "stdout:\n${runtime_stdout}\nstderr:\n${runtime_stderr}")
endif()
string(FIND "${runtime_stdout}"
    "AYTHER_STATUS {\"protocol_version\":1" status_prefix_at)
string(FIND "${runtime_stdout}"
    "\"reason\":\"cli.invalid_argument\"" status_reason_at)
if(status_prefix_at EQUAL -1 OR status_reason_at EQUAL -1)
    message(FATAL_ERROR
        "Installed Runtime did not emit the protocol-v1 CLI error:\n"
        "${runtime_stdout}\n${runtime_stderr}")
endif()

# Prove the executable is using its installed dependency set rather than the
# build tree. Remove each packaged shared library in isolation until the loader
# rejects startup before main(); at least one direct dependency must do so.
if(WIN32)
    file(GLOB dependency_candidates "${package_bin}/*.dll")
else()
    file(GLOB dependency_candidates
        "${package_bin}/*.so" "${package_bin}/*.so.*")
endif()
list(REMOVE_DUPLICATES dependency_candidates)
if(NOT dependency_candidates)
    message(FATAL_ERROR
        "Installed Runtime contains no shared dependency to test")
endif()

set(missing_dependency_rejected FALSE)
foreach(candidate IN LISTS dependency_candidates)
    set(negative_root "${SMOKE_DIR}-missing-dependency")
    file(REMOVE_RECURSE "${negative_root}")
    file(MAKE_DIRECTORY "${negative_root}")
    file(COPY "${SMOKE_DIR}/" DESTINATION "${negative_root}")
    get_filename_component(candidate_name "${candidate}" NAME)
    if(WIN32)
        file(REMOVE "${negative_root}/bin/${candidate_name}")
        set(negative_command "${negative_root}/bin/${runtime_name}")
    else()
        # One ELF dependency is commonly installed as a chain of linker-name,
        # SONAME, and fully-versioned symlinks. Remove that complete logical
        # family so the negative case cannot succeed through another alias.
        string(REGEX REPLACE "\\.so(\\..*)?$" "" dependency_stem
            "${candidate_name}")
        file(GLOB dependency_family
            "${negative_root}/bin/${dependency_stem}.so*")
        file(REMOVE ${dependency_family})
        set(negative_command "${CMAKE_COMMAND}" -E env
            --unset=LD_LIBRARY_PATH "${negative_root}/bin/${runtime_name}")
    endif()
    execute_process(
        COMMAND ${negative_command} --definitely-invalid-option
        WORKING_DIRECTORY "${clean_working_directory}"
        RESULT_VARIABLE negative_result
        OUTPUT_VARIABLE negative_stdout
        ERROR_VARIABLE negative_stderr)
    if(NOT negative_result EQUAL 64 OR
       NOT negative_stdout MATCHES "AYTHER_STATUS")
        set(missing_dependency_rejected TRUE)
        message(STATUS
            "Missing dependency rejected before Runtime protocol startup: ${candidate_name}")
        break()
    endif()
endforeach()
if(NOT missing_dependency_rejected)
    message(FATAL_ERROR
        "Removing every packaged shared library still allowed Runtime startup")
endif()
