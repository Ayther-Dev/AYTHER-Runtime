# ---------------------------------------------------------------------------
# AYTHER Runtime — game-session host.
#
# TWO MODES, one build definition (ADR-004 E8.1). Whether included from the
# monorepo or configured as a root project against an installed prefix, Runtime
# consumes Engine through
# `find_package(Ayther 0.1.0 CONFIG REQUIRED COMPONENTS engine)` and
# `Ayther::engine`. No source-tree path is part of this contract.
#
# Standalone mode, exercised by tools/runtime_oot_smoke.ps1:
#   cmake -S . -B build_rt -DCMAKE_PREFIX_PATH=<Engine bootstrap prefix>
#   cmake --build build_rt
# ---------------------------------------------------------------------------
set(AYTHER_RUNTIME_VERSION "0.1.0-beta.2")
set(AYTHER_RUNTIME_PROJECT_VERSION "0.1.0")
set(AYTHER_ENGINE_MIN_VERSION "0.1.0")
set(AYTHER_ENGINE_MAX_VERSION_EXCLUSIVE "0.2.0")
set(AYTHER_ENGINE_LOCKED_RELEASE "v0.1.0-rc.6")

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.21)
    project(AytherRuntime VERSION ${AYTHER_RUNTIME_PROJECT_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
    endif()
    enable_testing()
endif()

set(AYTHER_RUNTIME_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${AYTHER_RUNTIME_GENERATED_DIR}")
configure_file(
    cmake/ayther_runtime_version.h.in
    "${AYTHER_RUNTIME_GENERATED_DIR}/ayther_runtime_version.h"
    @ONLY
)
configure_file(
    cmake/ayther-runtime-package.json.in
    "${AYTHER_RUNTIME_GENERATED_DIR}/ayther-runtime-package.json"
    @ONLY
)

# Reject unsupported Windows frontends and Visual C++ toolsets before package
# discovery can obscure the ABI mismatch with a secondary dependency error.
if(WIN32)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/AytherWindowsToolchain.cmake")
    ayther_validate_windows_engine_toolchain(
        ENGINE_RELEASE "v0.1.0-rc.6"
        MIN_TOOLSET_VERSION "14.51"
    )
    enable_language(RC)
endif()

# Request the first-party `engine` surface because Runtime still needs the
# renderer, Vulkan backend, and RetroRunner, not only ADR-003's SDK API.
find_package(Ayther
    ${AYTHER_ENGINE_MIN_VERSION}...<${AYTHER_ENGINE_MAX_VERSION_EXCLUSIVE}
    CONFIG REQUIRED COMPONENTS engine)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/AytherEngineCompatibility.cmake")
ayther_validate_engine_package_contract(
    PACKAGE_VERSION "${Ayther_VERSION}"
    MIN_VERSION "${AYTHER_ENGINE_MIN_VERSION}"
    MAX_VERSION_EXCLUSIVE "${AYTHER_ENGINE_MAX_VERSION_EXCLUSIVE}"
    SYSTEM_NAME "${CMAKE_SYSTEM_NAME}"
    SYSTEM_PROCESSOR "${CMAKE_SYSTEM_PROCESSOR}"
    POINTER_SIZE "${CMAKE_SIZEOF_VOID_P}"
    COMPILER_ID "${CMAKE_CXX_COMPILER_ID}"
    COMPILER_VERSION "${CMAKE_CXX_COMPILER_VERSION}"
    SIMULATE_ID "${CMAKE_CXX_SIMULATE_ID}"
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(CheckCXXSourceCompiles)
    check_cxx_source_compiles(
        "#include <cstddef>\n#ifndef __GLIBCXX__\n#error libstdc++ required\n#endif\nint main() { return 0; }"
        AYTHER_ENGINE_USES_LIBSTDCXX)
    if(NOT AYTHER_ENGINE_USES_LIBSTDCXX)
        message(FATAL_ERROR
            "AYTHER Engine package contract failed: Linux requires Clang 18 "
            "with libstdc++, but __GLIBCXX__ is unavailable.")
    endif()
endif()
if(NOT DEFINED Ayther_SHADER_DIR OR Ayther_SHADER_DIR STREQUAL "")
    message(FATAL_ERROR
        "The installed Ayther engine package does not export "
        "Ayther_SHADER_DIR")
endif()
if(NOT IS_DIRECTORY "${Ayther_SHADER_DIR}")
    message(FATAL_ERROR
        "Ayther_SHADER_DIR does not name an installed shader directory: "
        "${Ayther_SHADER_DIR}")
endif()

# The rc.6 Windows archive is built with clang-cl over the MSVC v145 14.51
# ABI/STL. Prove compatibility with a real executable link against the package.
if(WIN32)
    ayther_verify_windows_engine_link(
        ENGINE_RELEASE "v0.1.0-rc.6"
        MIN_TOOLSET_VERSION "14.51"
        TARGET Ayther::engine
    )
endif()

find_package(SDL3         CONFIG REQUIRED)
find_package(Vulkan              REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(vk-bootstrap CONFIG REQUIRED)   # Runtime context + swapchain creation
find_package(imgui        CONFIG REQUIRED)   # overlay in-game (M4)
find_package(tomlplusplus CONFIG REQUIRED)   # explicit Engine ABI/runtime import

# stb is header-only, so express the direct Runtime dependency with an interface
# target. vcpkg installs the header from Runtime's own manifest; Engine is not
# part of the lookup or link contract.
find_package(Stb REQUIRED)
add_library(ayther_runtime_stb INTERFACE)
target_include_directories(ayther_runtime_stb SYSTEM INTERFACE
    "${Stb_INCLUDE_DIR}"
)

option(AYTHER_STRICT_WARNINGS "Treat first-party compiler warnings as errors" ON)
option(AYTHER_ENABLE_CLANG_TIDY "Run clang-tidy while compiling" OFF)
option(AYTHER_ENABLE_SANITIZERS "Enable AddressSanitizer and UBSan" OFF)
option(AYTHER_ENABLE_COVERAGE "Instrument first-party code for gcovr" OFF)
option(AYTHER_ENABLE_FUZZING "Build deterministic libFuzzer targets" OFF)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

if(AYTHER_STRICT_WARNINGS)
    if(MSVC)
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/W4>
                            $<$<COMPILE_LANGUAGE:CXX>:/WX>
                            $<$<COMPILE_LANGUAGE:CXX>:/permissive->
                            $<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
    else()
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wall>
                            $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
                            $<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>
                            $<$<COMPILE_LANGUAGE:CXX>:-Werror>)
    endif()
endif()

if(AYTHER_ENABLE_CLANG_TIDY)
    find_program(AYTHER_CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)
    set(AYTHER_CLANG_TIDY_COMMAND
        "${AYTHER_CLANG_TIDY_EXECUTABLE};--config-file=${CMAKE_CURRENT_SOURCE_DIR}/.clang-tidy")
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

if(AYTHER_ENABLE_SANITIZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        message(FATAL_ERROR "ASan/UBSan require Clang or GCC")
    endif()
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fsanitize=address,undefined>
                        $<$<COMPILE_LANGUAGE:CXX>:-fno-omit-frame-pointer>)
    add_link_options(-fsanitize=address,undefined)
endif()

if(AYTHER_ENABLE_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        message(FATAL_ERROR "Coverage instrumentation requires Clang or GCC")
    endif()
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:--coverage>
                        $<$<COMPILE_LANGUAGE:CXX>:-O0>
                        $<$<COMPILE_LANGUAGE:CXX>:-g>)
    add_link_options(--coverage)
endif()

if(AYTHER_ENABLE_FUZZING AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "AYTHER_ENABLE_FUZZING requires Clang/libFuzzer")
endif()

add_executable(ayther_runtime
    src/main.cpp
    src/runtime_application.cpp # MAD-039: process composition and orchestration
    src/status_emitter.cpp     # MAD-007: typed line-delimited JSON events
    src/runtime_options.cpp     # MAD-006: typed, strict command-line parsing
    src/runtime_config.cpp      # MIG-020: Runtime-owned data/save/capture paths
    src/input_map.cpp           # Play --input-map TOML → validated SDL bindings
    src/game_input.cpp          # SDL keyboard/gamepad → RetroPad bitfield (M3)
    src/player_overlay.cpp      # in-game pause menu + HD toggle (M4)
    src/player_config.cpp       # #299: per-game-and-pack player preferences
    src/capture.cpp             # #300: synchronized comparison capture
    src/capture_service.cpp     # MAD-041: transactional capture boundary
    src/save_state_store.cpp    # MAD-039: transactional persistence boundary
    src/diagnostics.cpp         # #302: audiovisual diagnostics assistant
    src/pack_layers.cpp         # #561: pack overlay-layer stack
    src/sonic_telemetry.cpp     # Runtime-only game RAM diagnostics
    # Runtime owns the complete window-side Vulkan lifetime and presentation
    # path. Engine receives only a borrowed VulkanContextView.
    src/vulkan_backend/spirv_file.cpp
    src/vulkan_backend/vk_result.cpp
    src/vulkan_backend/vulkan_calls.cpp
    src/vulkan_backend/vk_context.cpp
    src/vulkan_backend/vk_swapchain.cpp
    src/vulkan_backend/vk_present.cpp
    src/vulkan_backend/vk_postprocess.cpp
)

if(WIN32)
    target_sources(ayther_runtime PRIVATE
        resources/windows/ayther_runtime.rc
        resources/windows/AYTHER-Runtime.ico
    )
    set_source_files_properties(
        resources/windows/AYTHER-Runtime.ico
        PROPERTIES HEADER_FILE_ONLY TRUE
    )
endif()

if(AYTHER_ENABLE_CLANG_TIDY)
    set_property(TARGET ayther_runtime PROPERTY
        CXX_CLANG_TIDY "${AYTHER_CLANG_TIDY_COMMAND}")
endif()

target_include_directories(ayther_runtime PRIVATE
    src
    src/vulkan_backend
    "${AYTHER_RUNTIME_GENERATED_DIR}"
    # Public ayther/*.h and ayther/engine/*.hpp headers: Ayther::engine
)

# `Ayther::engine` owns the transitive Engine, Rust core, system-library, and
# bridge dependencies. Runtime intentionally depends only on the package target.
target_link_libraries(ayther_runtime PRIVATE
    Ayther::engine
    ayther_runtime_stb
    SDL3::SDL3
    Vulkan::Vulkan
    GPUOpen::VulkanMemoryAllocator
    vk-bootstrap::vk-bootstrap   # Runtime context + swapchain creation
    imgui::imgui                 # Dear ImGui — SDL3+Vulkan backends (M4 overlay)
    tomlplusplus::tomlplusplus   # exact runtime DLL through target metadata
    ${CMAKE_DL_LIBS}             # -ldl on Linux (no-op on Windows)
)

target_compile_definitions(ayther_runtime PRIVATE
    VMA_STATIC_VULKAN_FUNCTIONS=1
    VMA_DYNAMIC_VULKAN_FUNCTIONS=0
)

set_target_properties(ayther_runtime PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    INSTALL_RPATH "$ORIGIN"
)

# ---------------------------------------------------------------------------
# Stage runtime DLLs next to the exe.
#
# vcpkg's applocal helper shells out to dumpbin / llvm-objdump / objdump to walk
# the import table; when none is on PATH (e.g. a build outside a VS dev prompt)
# it silently no-ops, leaving SDL3.dll / tomlplusplus-3.dll / nfd.dll unstaged.
# The exe then fails to load with *no output at all* (STATUS_DLL_NOT_FOUND before
# main()).  TARGET_RUNTIME_DLLS (CMake 3.21+) resolves the DLLs straight from the
# linked imported targets — no external tool required, no hard-coded names that
# rot when a soname bumps.  System DLLs (vulkan-1.dll) are not listed; they
# resolve from System32 on a real machine.
# ---------------------------------------------------------------------------
if(WIN32)
    add_custom_command(TARGET ayther_runtime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:ayther_runtime> $<TARGET_FILE_DIR:ayther_runtime>
        COMMAND_EXPAND_LISTS
        COMMENT "Staging runtime DLLs (SDL3, nfd, ...) next to ayther_runtime"
    )
endif()

# ---------------------------------------------------------------------------
# SPIR-V shaders — pre-compiled .spv are committed next to their GLSL sources,
# so the build needs NO glslc / Vulkan SDK. They are staged to bin/shaders/ and
# located at runtime relative to SDL_GetBasePath(), keeping installs relocatable.
#
# Only the runtime's OWN shaders live here (postprocess: CRT + present). The
# ones AytherRenderer loads (sprite*, indexed_plane, video) belong to the motor
# and come from the installed package through its public `Ayther_SHADER_DIR`
# contract. The runtime never derives a source-tree or package-layout path for
# them (ADR-004 E8).
#
# After editing a .vert/.frag, regenerate its .spv (any glslc / glslangValidator):
#   glslc src/vulkan_backend/shaders/<name> -o src/vulkan_backend/shaders/<name>.spv
# ---------------------------------------------------------------------------
set(SHADER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/vulkan_backend/shaders)
set(SHADER_BIN_DIR ${CMAKE_BINARY_DIR}/bin/shaders)

file(GLOB_RECURSE AYTHER_ENGINE_SHADER_FILES
    LIST_DIRECTORIES false
    "${Ayther_SHADER_DIR}/*"
)
set(AYTHER_RUNTIME_SHADER_FILES
    "${SHADER_SRC_DIR}/postprocess.vert.spv"
    "${SHADER_SRC_DIR}/postprocess.frag.spv"
)
set(AYTHER_SHADER_STAGE_STAMP
    "${CMAKE_CURRENT_BINARY_DIR}/ayther_stage_shaders.stamp"
)

add_custom_command(
    OUTPUT "${AYTHER_SHADER_STAGE_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_BIN_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        "${Ayther_SHADER_DIR}" "${SHADER_BIN_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${AYTHER_RUNTIME_SHADER_FILES}
        "${SHADER_BIN_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E touch "${AYTHER_SHADER_STAGE_STAMP}"
    DEPENDS
        ${AYTHER_ENGINE_SHADER_FILES}
        ${AYTHER_RUNTIME_SHADER_FILES}
    COMMENT "Staging SPIR-V shaders (motor + runtime) to bin/shaders/"
    VERBATIM
)
add_custom_target(ayther_stage_shaders ALL
    DEPENDS "${AYTHER_SHADER_STAGE_STAMP}"
)
add_dependencies(ayther_runtime ayther_stage_shaders)

# Produce the same self-contained runtime layout that the package smoke tests.
# The install-time import scan follows exact linked artifacts and excludes the
# Windows/Linux system boundary.
if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
endif()
install(TARGETS ayther_runtime
    RUNTIME_DEPENDENCIES
        PRE_EXCLUDE_REGEXES
            "api-ms-.*"
            "ext-ms-.*"
        POST_EXCLUDE_REGEXES
            ".*[\\/][Ss][Yy][Ss][Tt][Ee][Mm]32[\\/].*"
            "^/lib/.*"
            "^/usr/lib/.*"
    RUNTIME DESTINATION bin COMPONENT Runtime
    LIBRARY DESTINATION bin COMPONENT Runtime
)
install(DIRECTORY "${Ayther_SHADER_DIR}/"
        DESTINATION bin/shaders COMPONENT Runtime)
install(FILES ${AYTHER_RUNTIME_SHADER_FILES}
        DESTINATION bin/shaders COMPONENT Runtime)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        DESTINATION share/licenses/ayther-runtime COMPONENT Runtime)
install(FILES "${AYTHER_RUNTIME_GENERATED_DIR}/ayther-runtime-package.json"
        DESTINATION share/ayther-runtime COMPONENT Runtime)

set(CPACK_PACKAGE_NAME "AYTHER-Runtime")
set(CPACK_PACKAGE_VENDOR "Ayther-Dev")
set(CPACK_PACKAGE_VERSION "${AYTHER_RUNTIME_PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME
    "ayther-runtime-${AYTHER_RUNTIME_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_GENERATOR "ZIP;TGZ")
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_ALL Runtime)
include(CPack)

# Runtime verification targets (ADR-004 section 3, E8) compile Runtime sources
# and therefore belong to this repository's test tree.
option(AYTHER_ENABLE_GPU_TESTS
    "Run SDL/Vulkan presentation tests on a real GPU" OFF)
add_subdirectory(tests)
