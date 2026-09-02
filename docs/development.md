# Development Guide

## Supported development shape

AYTHER Runtime is a C++20 CMake consumer of the installed AYTHER `engine`
package. It can be added by the parent monorepo or configured as a standalone
root project, but both modes must resolve the same `Ayther::engine` target.
Direct includes from an engine source tree are not an accepted substitute.
Public Engine headers must be included from the package include root (for
example, `<ayther/ayther_session.h>`), never by adding `include/ayther` to a
consumer include path.

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

The bootstrap also requires GitHub CLI (`gh`) authenticated for access to the
public artifact-attestation API. It does not require an Engine source checkout.

Dependency ownership is explicit in the manifest:

| Scope | Direct vcpkg entries |
| --- | --- |
| `Ayther::engine` native closure | `sdl3[vulkan]`, `vulkan`, `vulkan-memory-allocator`, `vk-bootstrap`, `tomlplusplus`, `zstd` |
| Runtime-owned | `imgui[sdl3-binding,vulkan-binding]`, `stb` |

All entries remain top-level because `AytherConfig.cmake` must resolve its
native packages while configuring a clean consumer. ImGui and stb are not part
of Engine's exported contract and must not be removed or made accidental
transitive dependencies.

Packages containing the `engine` component must export the absolute,
relocatable `Ayther_SHADER_DIR` from `AytherConfig.cmake`. Runtime validates
that contract during configuration and copies Engine SPIR-V only from that
installed directory; it never derives an Engine checkout or package-layout
path.

## Pinned AYTHER Engine artifact

[`dependencies/ayther-engine.lock.json`](../dependencies/ayther-engine.lock.json)
pins AYTHER Engine `v0.1.0-rc.4` for Linux and Windows x86_64, with and without
VPX. URLs and SHA-256 values are pinned from the official GitHub release assets.

Validate the complete lock without network access:

```powershell
& ./tools/bootstrap_ayther_engine.ps1 -ValidateOnly
```

Bootstrap the default Engine artifact for the current platform, or select the
VPX variant explicitly. The command returns the extracted CMake prefix:

```powershell
$enginePrefix = & ./tools/bootstrap_ayther_engine.ps1
$vpxPrefix = & ./tools/bootstrap_ayther_engine.ps1 `
  -Variant engine-vpx `
  -DestinationDirectory .deps/ayther-engine
```

An existing archive can be checked without downloading it:

```powershell
$enginePrefix = & ./tools/bootstrap_ayther_engine.ps1 `
  -Platform windows `
  -ArchivePath C:/downloads/ayther-engine-v0.1.0-rc.4-windows-x86_64.zip
```

The bootstrap fails if the filename, locked SHA-256, published release checksum,
SLSA predicate, signer workflow, or source tag differs from the lock. It then
extracts transactionally below `.deps/ayther-engine/prefixes/`. Existing cache
entries are verified and reused, never overwritten.

## Standalone build

```powershell
cmake -S . -B build -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  "-DCMAKE_PREFIX_PATH=$enginePrefix" `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

`CMAKE_PREFIX_PATH` must expose both the AYTHER package and any dependency
prefixes not supplied by the vcpkg toolchain. Configuration is expected to fail
when `Ayther::engine`, SDL3, Vulkan, Vulkan Memory Allocator, vk-bootstrap,
toml++, zstd, ImGui, or stb cannot be resolved; do not work around that failure
by adding private engine source paths.

After bootstrapping Engine, verify that the manifest still covers the package's
declared closure without accessing the network:

```powershell
& ./tools/verify_vcpkg_manifest.ps1 `
  -AytherConfig "$enginePrefix/lib/cmake/Ayther/AytherConfig.cmake"
```

## Tests

```powershell
ctest --test-dir build --output-on-failure
```

Current CTest coverage includes:

| Test | Contract covered |
| --- | --- |
| `ayther_engine_lock` | offline validation of release identity, checksum manifest, attestation policy, and supported artifact matrix |
| `vcpkg_manifest` | Engine package closure, pinned baseline, SDL3/ImGui features, and direct Runtime ownership of ImGui/stb |
| `runtime_config` | SDL/Engine-independent data paths and launcher save-root precedence |
| `player_config` | key generation, defaults, persistence distinctions, and tolerant parsing |
| `split_geometry` | comparison geometry and zero-width edge cases without a GPU |
| `capture` | image-source selection and metadata that excludes protected content |
| `pack_layers` | default/custom layer composition and deterministic ordering |
| `diagnostics` | pure recommendation rules without SDL or Vulkan |
| `probe_core` | real process exit codes and one-line `AYTHER_STATUS` core-probe output |

The test suite does not establish full GPU-driver compatibility, frame-perfect
timing, broad Libretro-core compatibility, or release readiness.

## Published-package smoke test

From the Runtime repository root:

```powershell
& ./tools/runtime_oot_smoke.ps1 `
  -ToolchainFile "$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
```

The script bootstraps the pinned published package, passes its prefix to CMake,
builds Runtime in a temporary directory, verifies packaged shaders, and runs
CTest. It never resolves or reads an Engine/monorepo source tree. Use `-Keep` to
preserve its temporary build directory for inspection, or `-ConfigureOnly` to
exercise only dependency bootstrap and CMake package discovery.

> [!CAUTION]
> The smoke script removes only the unique temporary build directory it creates.
> Its verified download and extraction cache remains below `.deps/`.

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
