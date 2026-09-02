# Development Guide

## Supported development shape

AYTHER Runtime is a C++20 CMake consumer of the installed AYTHER `engine`
package. It can be added by the parent monorepo or configured as a standalone
root project, but both modes must resolve the same `Ayther::engine` target.
Direct includes from an engine source tree are not an accepted substitute.

The current repository is an early engineering snapshot. The documented build
is a maintainer workflow, not a supported end-user installation procedure.

## Toolchain and dependencies

Required:

- CMake 3.21 or newer;
- a C++20 compiler;
- Vulkan headers, loader, and a working driver for interactive runs;
- an installed AYTHER package with the `engine` component; and
- the vcpkg dependencies pinned by `vcpkg.json`.

Ninja and PowerShell 7 are recommended for the reference workflow. On Windows,
the post-build step stages imported runtime DLLs next to the executable. System
Vulkan DLLs are expected to resolve from the operating system.

## Pinned AYTHER Engine artifact

[`dependencies/ayther-engine.lock.json`](../dependencies/ayther-engine.lock.json)
pins AYTHER Engine `v0.1.0-rc.4` for Linux and Windows x86_64, with and without
VPX. URLs and SHA-256 values come from the immutable GitHub release assets.

Validate the complete lock without network access:

```powershell
pwsh tools/fetch_ayther_engine.ps1 -ValidateOnly
```

Download the default Engine artifact for the current platform, or select the
VPX variant explicitly:

```powershell
pwsh tools/fetch_ayther_engine.ps1 -DestinationDirectory .deps
pwsh tools/fetch_ayther_engine.ps1 `
  -Variant engine-vpx `
  -DestinationDirectory .deps
```

An existing archive can be checked without downloading it:

```powershell
pwsh tools/fetch_ayther_engine.ps1 `
  -Platform windows `
  -ArchivePath .deps/ayther-engine-v0.1.0-rc.4-windows-x86_64.zip
```

The command fails if the filename or SHA-256 differs from the lock. Extraction
and wiring the resulting prefix into `CMAKE_PREFIX_PATH` remain explicit build
steps.

## Standalone build

```powershell
cmake -S . -B build -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DCMAKE_PREFIX_PATH="C:/path/to/installed/ayther" `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

`CMAKE_PREFIX_PATH` must expose both the AYTHER package and any dependency
prefixes not supplied by the vcpkg toolchain. Configuration is expected to fail
when `Ayther::engine`, SDL3, Vulkan, vk-bootstrap, ImGui, or stb cannot be
resolved; do not work around that failure by adding private engine source paths.

## Tests

```powershell
ctest --test-dir build --output-on-failure
```

Current CTest coverage includes:

| Test | Contract covered |
| --- | --- |
| `player_config` | key generation, defaults, persistence distinctions, and tolerant parsing |
| `split_geometry` | comparison geometry and zero-width edge cases without a GPU |
| `capture` | image-source selection and metadata that excludes protected content |
| `pack_layers` | default/custom layer composition and deterministic ordering |
| `diagnostics` | pure recommendation rules without SDL or Vulkan |
| `probe_core` | real process exit codes and one-line `AYTHER_STATUS` core-probe output |

The test suite does not establish full GPU-driver compatibility, frame-perfect
timing, broad Libretro-core compatibility, or release readiness.

## Parent-tree package smoke test

From the AYTHER monorepo root, after building the engine package:

```powershell
pwsh runtime/tools/runtime_oot_smoke.ps1 -BuildDir build
```

The script installs the configured engine package, builds Runtime in a temporary
directory with no engine source include path, verifies packaged
shaders, and runs CTest. Use `-Keep` to preserve its temporary directories for
inspection.

> [!CAUTION]
> The smoke script deletes and recreates its own fixed directories beneath the
> operating-system temporary directory. Do not repurpose those paths for data
> that must be retained.

## Shader workflow

Runtime commits SPIR-V files beside its GLSL sources so consumers do not need a
shader compiler to build:

```powershell
glslc src/vulkan_backend/shaders/postprocess.vert `
  -o src/vulkan_backend/shaders/postprocess.vert.spv
glslc src/vulkan_backend/shaders/postprocess.frag `
  -o src/vulkan_backend/shaders/postprocess.frag.spv
```

Whenever GLSL changes, regenerate and commit the matching SPIR-V binaries in the
same change. The build copies engine shaders from `Ayther_SHADER_DIR` and Runtime
post-process shaders into `build/bin/shaders/`.

## Debugging and diagnostics

- Invoke Runtime without required arguments to print the implemented CLI usage.
- Use `--probe-core <path>` to isolate dynamic-load and Libretro-symbol failures
  without initializing SDL.
- Use `--frames N` for bounded development and CI sessions.
- Use `--capture-at N[,M...]` for deterministic comparative captures.
- Treat only complete `AYTHER_STATUS ` lines as launcher protocol; all other
  stdout and stderr content is diagnostic logging.
- A process that terminates without an `exit` event is an abnormal session.

## Change discipline

- Preserve the Runtime/Engine ownership boundary documented in
  [architecture.md](architecture.md).
- Keep `FrameView` borrowing rules explicit at every new call site.
- Add tests around pure policy and geometry before introducing GPU-dependent
  coverage.
- Update source, tests, CLI documentation, and protocol documentation together.
- Do not commit ROMs, cores, packs, captures containing protected content,
  secrets, local configuration, build trees, or generated IDE state.

See [CONTRIBUTING.md](../CONTRIBUTING.md) for the review checklist, MPL-2.0
contribution terms, and third-party content restrictions.
