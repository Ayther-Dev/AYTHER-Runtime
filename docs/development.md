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
- on Windows, Microsoft `cl` or LLVM `clang-cl` using the MSVC ABI, STL and
  linker from toolset v145 14.51+ (Visual Studio 2026), plus a matching or
  newer Visual C++ Redistributable; the published Engine archive was compiled
  with `clang-cl` 20.1.8 over that toolset; MinGW/GNU is not supported by the
  current Windows artifact;
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
| `Ayther::engine` native closure | `sdl3[vulkan]`, `vulkan`, `vulkan-memory-allocator`, `tomlplusplus`, `zstd` |
| Runtime-owned | `vk-bootstrap`, `imgui[sdl3-binding,vulkan-binding]`, `stb` |

All entries remain top-level because `AytherConfig.cmake` must resolve its
native packages while configuring a clean consumer. ImGui and stb are not part
of Engine's exported contract and must not be removed or made accidental
transitive dependencies. Runtime resolves stb with `find_package(Stb)`, exposes
it internally through `ayther_runtime_stb`, and compiles the PNG writer with
translation-unit-local linkage in `capture.cpp`.

Packages containing the `engine` component must export the absolute,
relocatable `Ayther_SHADER_DIR` from `AytherConfig.cmake`. Runtime validates
that contract during configuration and copies Engine SPIR-V only from that
installed directory; it never derives an Engine checkout or package-layout
path.

## Pinned AYTHER Engine artifact

[`dependencies/ayther-engine.lock.json`](../dependencies/ayther-engine.lock.json)
pins AYTHER Engine `v0.1.0-rc.6` for Linux and Windows x86_64, with and without
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
  -ArchivePath C:/downloads/ayther-engine-v0.1.0-rc.6-windows-x86_64.zip
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

### Windows compiler and ABI contract

The Windows `v0.1.0-rc.6` package has two independent compatibility axes:

- **Frontend:** Microsoft `cl` or LLVM `clang-cl` is accepted. Engine itself
  was built with `clang-cl` 20.1.8, but Runtime does not require that exact
  frontend or version.
- **ABI/toolset:** both frontends must consume Visual C++ v145 14.51 or newer
  headers, STL, libraries and linker. This is the immediate binary contract.
  MinGW/GNU uses a different ABI and is rejected for this artifact.

During configure, `cmake/AytherWindowsToolchain.cmake` reports the detected
frontend and toolset. After validating the minimum version, it performs a real
`try_compile` executable link against `Ayther::engine`; merely compiling an
Engine header is not considered sufficient. A failure names the detected
toolset, the 14.51 minimum, Engine `v0.1.0-rc.6`, and the supported remedies.

For the reference setup, launch a Visual Studio 2026 Developer PowerShell or
Developer Command Prompt with v145 14.51+ selected before configuring. To use
clang-cl, select the same developer environment and pass `clang-cl` as the C++
compiler so it keeps the Visual C++ headers, libraries and linker. If that
toolset cannot be installed, use or rebuild an Engine artifact for the intended
ABI rather than attempting to link the current archive with MinGW.

After bootstrapping Engine, verify that the manifest still covers the package's
declared closure without accessing the network:

```powershell
& ./tools/verify_vcpkg_manifest.ps1 `
  -AytherConfig "$enginePrefix/lib/cmake/Ayther/AytherConfig.cmake"
```

## Pull request CI presets

`CMakePresets.json` provides the two RelWithDebInfo presets used by pull request
CI: `windows-ci` selects Visual Studio 2026 with the v145 toolset, while
`linux-ci` selects Ninja. They require CMake 3.25+, PowerShell 7, `VCPKG_ROOT`,
and an Engine prefix produced from the locked `v0.1.0-rc.6` artifact:

```powershell
$env:AYTHER_ENGINE_PREFIX = & ./tools/bootstrap_ayther_engine.ps1 `
  -Variant engine `
  -DestinationDirectory .deps/ayther-engine
$env:VCPKG_ROOT = "C:/dev/vcpkg"

cmake --preset windows-ci
cmake --build --preset windows-ci
ctest --preset windows-ci --output-junit "$PWD/out/windows-ci-junit.xml"
```

On Linux, export the same two environment variables and substitute `linux-ci`
in all three commands. The presets disable the CMake user and system package
registries so `Ayther::engine` is resolved from the bootstrapped prefix rather
than an undeclared local installation.

`.github/workflows/ci.yml` runs those commands for pull requests targeting
`main` on Windows and Linux. Each job uploads its bootstrap, configure, build,
and test logs together with CTest's `LastTest.log` and JUnit report, including
when an earlier step fails. External actions are pinned to full commit SHAs and
the workflow token has only `contents: read` and `attestations: read`.

After two successful pull request executions on the same proposed CI setup,
configure the `main` branch protection or ruleset to require both stable check
names:

- `Windows / RelWithDebInfo`
- `Linux / RelWithDebInfo`

Do not enable those required checks before GitHub has observed both contexts;
otherwise every pull request will be blocked by checks that cannot yet be
selected or satisfied. Debug and analysis presets remain part of MAD-004 and
are intentionally outside this CI-only change.

## Tests

```powershell
ctest --test-dir build --output-on-failure
```

Current CTest coverage includes:

| Test | Contract covered |
| --- | --- |
| `ayther_engine_lock` | offline validation of release identity, checksum manifest, attestation policy, and supported artifact matrix |
| `vcpkg_manifest` | Engine package closure, pinned baseline, SDL3/ImGui features, and direct Runtime ownership of ImGui/stb |
| `ci_workflow_contract` | PR triggers, least-privilege permissions, SHA-pinned actions, locked Engine bootstrap, retained diagnostics, and CI preset invariants |
| `windows_toolchain_contract` | accepted `cl`/`clang-cl` frontends, v145 14.51 minimum, MinGW rejection, and complete failure diagnostics |
| `runtime_config` | SDL/Engine-independent data paths and launcher save-root precedence |
| `status_emitter` | typed event shapes, byte-exact JSON escaping, single-record writes, and flush behavior |
| `status_emitter_json` | all event fixtures parsed by CMake's real JSON parser, including hostile text and UTF-8 round trips |
| `status_emitter_source_contract` | rejects protocol framing anywhere below `src/` except `status_emitter.cpp` |
| `output_profile` | Runtime-owned presets, selection precedence, scaling geometry, filtering, and shader mixing |
| `player_config` | key generation, defaults, persistence distinctions, and tolerant parsing |
| `split_geometry` | comparison geometry and zero-width edge cases without a GPU |
| `capture` | image-source selection, metadata that excludes protected content, and standalone stb linkage without Engine |
| `pack_layers` | default/custom layer composition and deterministic ordering |
| `diagnostics` | pure recommendation rules without SDL or Vulkan |
| `core_probe_api` | installed `CoreProbe` ownership, JSON, and loader-error contract |
| `probe_core` | codes 0/2/3 and parsed `AYTHER_STATUS` JSON using a deterministic synthetic Libretro core, a missing path, and a loadable non-core library |
| `runtime_oot_smoke_contract` | standalone source-root discovery, explicit Engine package inputs, and mutual exclusion |

The test suite does not establish full GPU-driver compatibility, frame-perfect
timing, broad Libretro-core compatibility, or release readiness.

## Published-package smoke test

Use an installed Engine package prefix:

```powershell
& ./tools/runtime_oot_smoke.ps1 `
  -AytherPrefix C:/deps/ayther-engine `
  -ToolchainFile "$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
```

Or provide a downloaded Engine release archive for checksum and attestation
verification followed by extraction:

```powershell
& ./tools/runtime_oot_smoke.ps1 `
  -EngineArchive C:/downloads/ayther-engine-v0.1.0-rc.6-windows-x86_64.zip `
  -ToolchainFile "$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
```

The two inputs are mutually exclusive. With neither input, the script downloads
the artifact pinned by Runtime. It derives the Runtime source root from
`$PSScriptRoot/..`, so the command may be launched from any working directory
inside or outside an independent Runtime clone. The selected package prefix is
passed to CMake before Runtime is built in a temporary directory, packaged
shaders are verified, and CTest runs. No Engine/monorepo source tree is resolved
or read. Use `-Keep` to preserve the temporary build directory for inspection,
or `-ConfigureOnly` to exercise only bootstrap and CMake package discovery.

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
- Use `--probe-core <path>` to exercise Engine's public RAII probe and isolate
  dynamic-load and Libretro-symbol failures without initializing SDL.
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
