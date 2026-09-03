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
set(AYTHER_RUNTIME_VERSION "0.1.0")

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.21)
    project(AytherRuntime VERSION ${AYTHER_RUNTIME_VERSION} LANGUAGES CXX)
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

# Reject unsupported Windows frontends and Visual C++ toolsets before package
# discovery can obscure the ABI mismatch with a secondary dependency error.
if(WIN32)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/AytherWindowsToolchain.cmake")
    ayther_validate_windows_engine_toolchain(
        ENGINE_RELEASE "v0.1.0-rc.6"
        MIN_TOOLSET_VERSION "14.51"
    )
endif()

# Request the first-party `engine` surface because Runtime still needs the
# renderer, Vulkan backend, and RetroRunner, not only ADR-003's SDK API.
find_package(Ayther 0.1.0 CONFIG REQUIRED COMPONENTS engine)
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

# stb is header-only, so express the direct Runtime dependency with an interface
# target. vcpkg installs the header from Runtime's own manifest; Engine is not
# part of the lookup or link contract.
find_package(Stb REQUIRED)
add_library(ayther_runtime_stb INTERFACE)
target_include_directories(ayther_runtime_stb SYSTEM INTERFACE
    "${Stb_INCLUDE_DIR}"
)

add_executable(ayther_runtime
    src/main.cpp
    src/status_emitter.cpp     # MAD-007: typed line-delimited JSON events
    src/runtime_config.cpp      # MIG-020: Runtime-owned data/save/capture paths
    src/game_input.cpp          # SDL keyboard/gamepad → RetroPad bitfield (M3)
    src/player_overlay.cpp      # in-game pause menu + HD toggle (M4)
    src/player_config.cpp       # #299: per-game-and-pack player preferences
    src/capture.cpp             # #300: synchronized comparison capture
    src/diagnostics.cpp         # #302: audiovisual diagnostics assistant
    src/pack_layers.cpp         # #561: pack overlay-layer stack
    src/sonic_telemetry.cpp     # Runtime-only game RAM diagnostics
    # Runtime owns the complete window-side Vulkan lifetime and presentation
    # path. Engine receives only a borrowed VulkanContextView.
    src/vulkan_backend/vk_context.cpp
    src/vulkan_backend/vk_swapchain.cpp
    src/vulkan_backend/vk_present.cpp
    src/vulkan_backend/vk_postprocess.cpp
)

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
    ${CMAKE_DL_LIBS}             # -ldl on Linux (no-op on Windows)
)

target_compile_definitions(ayther_runtime PRIVATE
    VMA_STATIC_VULKAN_FUNCTIONS=1
    VMA_DYNAMIC_VULKAN_FUNCTIONS=0
)

set_target_properties(ayther_runtime PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
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

# tomlplusplus is a *compiled* vcpkg port (tomlplusplus-3.dll) pulled in PRIVATEly
# through the ayther_engine static lib, so TARGET_RUNTIME_DLLS does not see it —
# yet the exe imports it, and missing it makes ayther_runtime fail to load with no
# output.  Copy it explicitly (glob so a soname bump doesn't break this).
set(AYTHER_TOMLXX_DLLS "")
if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
    file(GLOB AYTHER_TOMLXX_DLLS
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/tomlplusplus*.dll")
endif()
if(AYTHER_TOMLXX_DLLS)
    add_custom_command(TARGET ayther_runtime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${AYTHER_TOMLXX_DLLS} $<TARGET_FILE_DIR:ayther_runtime>
        COMMAND_EXPAND_LISTS
        COMMENT "Staging tomlplusplus DLL next to ayther_runtime"
    )
endif()

# ---------------------------------------------------------------------------
# SPIR-V shaders — pre-compiled .spv are committed next to their GLSL sources,
# so the build needs NO glslc / Vulkan SDK. They are staged to bin/shaders/ and
# loaded at runtime via AYTHER_SHADER_DIR (absolute path, CWD-independent).
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

target_compile_definitions(ayther_runtime PRIVATE
    AYTHER_SHADER_DIR="${SHADER_BIN_DIR}/"
)

# Runtime verification targets (ADR-004 section 3, E8) compile Runtime sources
# and therefore belong to this repository's test tree.
add_subdirectory(tests)
