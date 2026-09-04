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

# An intentional CLI error proves the packaged process reached main(). A
# missing transitive DLL would fail in the Windows loader before producing the
# stable EX_USAGE-compatible status.
execute_process(
    COMMAND "${packaged_runtime}" --definitely-invalid-option
    WORKING_DIRECTORY "${package_bin}"
    RESULT_VARIABLE runtime_result
    OUTPUT_VARIABLE runtime_stdout
    ERROR_VARIABLE runtime_stderr
)
if(NOT runtime_result EQUAL 64)
    message(FATAL_ERROR
        "Packaged Runtime did not reach CLI parsing (exit=${runtime_result}).\n"
        "stdout:\n${runtime_stdout}\nstderr:\n${runtime_stderr}")
endif()
