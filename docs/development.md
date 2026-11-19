# Development guide

This guide is for contributors who build and test AYTHER Runtime from source.
It is a maintainer workflow, not an end-user installation procedure. For what
Runtime owns and how a session runs, read [Architecture](architecture.md).

## Requirements

| Requirement | Notes |
| --- | --- |
| CMake 3.25 or newer | Needed by `CMakePresets.json`. A plain `cmake -S . -B ...` configure needs 3.21. |
| Windows: Visual Studio 2026, toolset v145 14.51 or newer | `cl` or `clang-cl` with the MSVC ABI, STL and linker. Older toolsets and MinGW/GNU are rejected at configure time. |
| Linux: Ubuntu 24.04 with Clang 18 | Engine's Linux package is built for the Clang 18 + libstdc++ ABI. |
| vcpkg | `VCPKG_ROOT` must point to it; dependencies come from `vcpkg.json` in manifest mode. |
| PowerShell 7 | Bootstrap scripts and several contract tests. |
| GitHub CLI (`gh`), authenticated | The Engine bootstrap verifies SLSA provenance with `gh attestation verify`. |
| Vulkan loader and driver | Needed for interactive runs and GPU tests only. |

Ninja is used by the Linux presets. No Engine source checkout is needed.

### Dependencies

| Scope | vcpkg entries |
| --- | --- |
| `Ayther::engine` native closure | `sdl3[vulkan]`, `vulkan`, `vulkan-memory-allocator`, `tomlplusplus`, `zstd` |
| Runtime-owned | `vk-bootstrap`, `imgui[sdl3-binding,vulkan-binding,sdl3-renderer-binding]`, `stb` |

All entries are top-level because `AytherConfig.cmake` resolves its native
packages while a clean consumer configures. ImGui and stb are Runtime
dependencies, not part of Engine's exported contract; do not drop them or rely
on them transitively. `capture.cpp` owns the stb PNG writer.

Check that the manifest still covers the Engine package's declared closure
(offline):

```powershell
& ./tools/verify_vcpkg_manifest.ps1 -AytherConfig "$env:AYTHER_ENGINE_PREFIX/lib/cmake/Ayther/AytherConfig.cmake"
```

## Get the Engine package

Runtime consumes AYTHER Engine as an installed CMake package pinned by
`dependencies/ayther-engine.lock.json`. The locks, the verification they
perform and the Tessera route are described in
[Runtime–Engine compatibility](runtime-engine-compatibility.md).

```powershell
# Validate the lock offline
./tools/bootstrap_ayther_engine.ps1 -ValidateOnly

# Download, verify and extract; prints the CMake prefix
$env:AYTHER_ENGINE_PREFIX = & ./tools/bootstrap_ayther_engine.ps1 -Variant engine-vpx
```

`-Variant engine` (the default) selects the package without VP9 decoding;
`engine-vpx` adds it and is what the Windows release archive uses. To verify an
archive you already downloaded, pass `-ArchivePath <zip>`.

Packages that contain the `engine` component export an absolute, relocatable
`Ayther_SHADER_DIR`. Runtime validates it during configuration and copies Engine
SPIR-V only from that directory. Include public Engine headers from the package
include root (for example `<ayther/ayther_session.h>`); never add
`include/ayther` or an Engine source path to the include path.

## Build and test

With `AYTHER_ENGINE_PREFIX` and `VCPKG_ROOT` set:

```powershell
cmake --preset windows-ci
cmake --build --preset windows-ci
ctest --preset windows-ci --output-on-failure

# List tests, or run a subset
ctest --preset windows-ci -N
ctest --preset windows-ci -R "runtime_options" --output-on-failure
```

The executable and staged assets (Engine and Runtime shaders, fonts, imported
DLLs on Windows) are in `out/build/<preset>/bin/<config>/`.

| Configure preset | Platform | Purpose |
| --- | --- | --- |
| `windows-ci` | Windows | Visual Studio 2026, v145, RelWithDebInfo; the pull-request and release build. |
| `windows-qa` | Windows | `windows-ci` against the QA Engine package; see [Replay QA](#replay-qa). |
| `windows-qa-gpu` | Windows | `windows-qa` plus the GPU and desktop-session tests. |
| `linux-ci` | Linux | Ninja, Clang, RelWithDebInfo; the Linux release build. |
| `linux-quality` | Linux | `linux-ci` with clang-tidy and coverage. |
| `linux-sanitizers` | Linux | `linux-ci` with ASan, UBSan and libFuzzer. |

Every preset builds in `out/build/<preset>` and disables the CMake user and
system package registries, so `Ayther::engine` comes only from
`AYTHER_ENGINE_PREFIX`. Each has matching build and test presets.

Useful CMake options:

| Option | Default | Effect |
| --- | --- | --- |
| `AYTHER_STRICT_WARNINGS` | `ON` | `/W4 /WX /permissive-` or `-Wall -Wextra -Wpedantic -Werror`. |
| `AYTHER_ENABLE_GPU_TESTS` | `OFF` | Adds the SDL/Vulkan tests that need a GPU. |
| `AYTHER_ENABLE_TESSERA_NATIVE` | `OFF` | Native QA driver and Tessera integration tests; needs the native packages ([how](runtime-engine-compatibility.md#tessera-route-windows-x64)). |
| `AYTHER_ENABLE_CLANG_TIDY`, `AYTHER_ENABLE_COVERAGE`, `AYTHER_ENABLE_SANITIZERS`, `AYTHER_ENABLE_FUZZING` | `OFF` | Analysis builds; see [Quality baseline](quality-baseline.md). |

### Test labels

CTest names are the reference for coverage; list them with `ctest --preset
<preset> -N`. Useful label filters:

| Label | Tests |
| --- | --- |
| `audio_qa` | Replay QA checker, protocol and evidence tests (QA build). |
| `gpu` | Tests that need a GPU and a desktop session. |
| `native`, `release` | Lock and release-workflow contract tests. |
| `tessera` | Tessera integration tests (`AYTHER_ENABLE_TESSERA_NATIVE=ON`). |
| `replay_inspection` | Replay inspection unit tests. |

The suite does not establish GPU-driver compatibility, frame-perfect timing,
broad core compatibility or release readiness.

### Published-package smoke test

`tools/runtime_oot_smoke.ps1` builds and tests Runtime out of tree against an
installed Engine prefix (`-AytherPrefix`) or a downloaded Engine archive
(`-EngineArchive`), which it verifies and extracts first. The two inputs are
mutually exclusive; with neither, it downloads the locked artifact. It runs
from any working directory, removes only its own temporary build directory, and
keeps the verified download cache below `.deps/`. `-Keep` preserves the build
directory; `-ConfigureOnly` stops after package discovery.

```powershell
& ./tools/runtime_oot_smoke.ps1 -AytherPrefix $env:AYTHER_ENGINE_PREFIX `
  -ToolchainFile "$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
```

Installed layout and the package smoke test are described in
[Runtime package layout](packaging.md).

## Replay QA

The audio and replay QA tools (`ayther_audio_qa`, the launcher
`ayther_replay_qa` and Runtime's replay inspection) exist only in a build
against the QA Engine package: the Windows `engine-vpx` artifact of the Engine
release, pinned separately by `dependencies/ayther-engine.qa.lock.json`.

```powershell
# Verify the QA lock and extract the pinned QA Engine package
./tools/bootstrap_ayther_engine_qa.ps1 -ValidateOnly
$env:AYTHER_ENGINE_PREFIX = & ./tools/bootstrap_ayther_engine_qa.ps1

# Configure, build and test without GPU (the `Windows / QA` check)
cmake --preset windows-qa
cmake --build --preset windows-qa
ctest --preset windows-qa -L audio_qa -LE gpu --output-on-failure

# The same plus the tests that need a GPU and a desktop session
cmake --preset windows-qa-gpu
cmake --build --preset windows-qa-gpu
ctest --preset windows-qa-gpu --output-on-failure
```

- **Presets.** `windows-qa` inherits `windows-ci` and sets
  `AYTHER_REQUIRE_AUDIO_QA_ENGINE_PACKAGE=ON`. `windows-qa-gpu` adds
  `AYTHER_ENABLE_GPU_TESTS=ON`: the visible replay, the replay inspection with
  simulated events, and the launcher window and interface smoke tests.
- **vcpkg.** The launcher draws its window with Dear ImGui's SDL_Renderer
  backend, so `vcpkg.json` enables the `imgui` feature `sdl3-renderer-binding`
  next to `sdl3-binding` and `vulkan-binding`.
- **Commands.** `ayther_audio_qa check --runtime <exe> --rom <rom> --take
  <take.ayr> --output <dir>` replays takes and keeps the evidence;
  `ayther_audio_qa options --format toml` prints the option inventory. See
  [`tools/audio_qa/README.md`](../tools/audio_qa/README.md),
  [`tools/replay_qa_launcher/README.md`](../tools/replay_qa_launcher/README.md)
  and [Audio QA evidence](audio-qa-evidence.md).
- **Test hooks.** Read only inside `--qa-session`, never in an ordinary run:
  - `AYTHER_QA_INPUT_SCRIPT=<file>` scripts the replay window: keys, focus,
    faults, closing, damaged checkpoints and overlay scrolling, each triggered
    by `frame=<k>`, `paused=<k>` or `after=<ms>`. The format is in
    `src/replay_inspection/input_script.h`.
  - `AYTHER_QA_TIMING_LOG=<file>` writes the time marks of the key router and
    the presentation, used by `tests/audio_qa/inspection_measurement.cpp`.

## Continuous integration

| Workflow | Check | Runs |
| --- | --- | --- |
| `ci.yml` | `Windows / RelWithDebInfo` | `windows-ci` build, an incremental no-op build, full CTest. |
| `ci.yml` | `Linux / RelWithDebInfo` | `linux-quality`: clang-format 18 on changed C++ files, clang-tidy, CTest, coverage floor. |
| `ci.yml` | `Linux / ASan-UBSan-Fuzz` | `linux-sanitizers`: sanitizer build, CTest, fuzz smoke. |
| `qa.yml` | `Windows / QA` | `windows-qa` build and `ctest -L audio_qa -LE gpu`. |
| `security.yml` | `Security / Dependency Review`, `Security / Secret Scan` | See [SECURITY.md](../SECURITY.md). |
| `release.yml` | `Release / *` | On a `v*-beta.*` tag: builds, tests and packages the three archives and publishes a prerelease with `docs/releases/<tag>.md` as notes. |

Pull requests to `main` run the build and test jobs. On pushes to `main` and a
weekly schedule the jobs only configure, to keep the vcpkg binary cache warm.
Each job uploads its bootstrap, configure, build, test and CTest logs, including
when a step fails. Actions are pinned to full commit SHAs and the workflow
token is read-only. The required checks for `main` are listed in
[Project status](status.md#latest-validation).

Run the formatting check locally the way CI does (`clang-format` 18 formats
whole changed files):

```bash
CLANG_FORMAT=clang-format-18 bash tools/run_clang_format_changed.sh origin/main HEAD
```

## Shaders

Runtime commits SPIR-V next to its GLSL sources so a build needs no shader
compiler. When a GLSL file changes, regenerate and commit its SPIR-V in the
same change:

```powershell
glslc src/vulkan_backend/shaders/postprocess.vert -o src/vulkan_backend/shaders/postprocess.vert.spv
glslc src/vulkan_backend/shaders/postprocess.frag -o src/vulkan_backend/shaders/postprocess.frag.spv
```

The build copies Engine shaders from `Ayther_SHADER_DIR` and Runtime's
post-process shaders into `bin/shaders/`.

## Run and debug

AYTHER Play CE normally launches Runtime. Direct invocations for development:

```powershell
$exe = "out/build/windows-ci/bin/RelWithDebInfo/ayther_runtime.exe"

# Libretro core
& $exe --core <core.dll> --rom <game.md> [--pack <pack.ay>]

# Tessera (needs a Tessera-route build)
& $exe --provider tessera --rom <game.md>

# Probe a core without SDL or a ROM
& $exe --probe-core <core.dll>
```

- Running without the required options prints the usage text.
- `--frames N` ends a session after N frames; `--capture-at N[,M...]` takes
  synchronized captures.
- `--input-script <file>` drives port 0 from power-on with a pad timeline (the
  grammar of `tools/audio_qa/pad_timeline.h`); the run ends with the script's
  last frame. `--frame-log <csv>` writes one timing row per displayed frame.
  Together they time the same game reproducibly under both providers.
- Only complete `AYTHER_STATUS ` lines are protocol; everything else on stdout
  and stderr is a diagnostic log. A process that ends without an `exit` event
  is an abnormal session.

All options are specified in the [process and CLI contract](process-protocol.md).

## Change discipline

- Keep the Runtime/Engine ownership boundary in [Architecture](architecture.md).
- Keep `FrameView` borrowing rules explicit at every new call site.
- Test pure policy and geometry before adding GPU-dependent coverage.
- Change source, tests, the CLI contract and the protocol documents together,
  and add a `CHANGELOG.md` entry under `[Unreleased]`.
- Do not commit ROMs, cores, packs, captures with protected content, secrets,
  local configuration, build trees or IDE state.

See [CONTRIBUTING.md](../CONTRIBUTING.md) for the review checklist and the
contribution terms.
