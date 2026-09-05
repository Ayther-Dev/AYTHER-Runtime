# AYTHER Runtime

<img alt="AYTHER Runtime" height="128" src="docs/assets/branding/ayther-runtime-logo.svg" width="128"/>

AYTHER Runtime is the C++20 game-session host launched by AYTHER Play. It loads
a user-supplied Libretro core and game image, drives the AYTHER engine, renders
the original and HD-composed frame paths through Vulkan, and owns the in-game
presentation and controls.

> [!WARNING]
> **Beta:** product version `0.1.0-beta.2` is an internal evaluation release.
> APIs, command-line options, package boundaries, configuration formats, and
> saved-state compatibility may change without notice. The Runtime–Play process
> protocol v1 is stable, but that narrow wire guarantee does not make the
> product generally available or production-ready.

> [!IMPORTANT]
> Runtime source is licensed under the Mozilla Public License 2.0. Third-party
> dependencies, AYTHER products maintained in other repositories, user-supplied
> cores and content, and AYTHER trademarks remain outside that grant unless
> their rights holder states otherwise. See [Legal and content boundaries](#legal-and-content-boundaries).

## Role in the AYTHER system

```text
AYTHER Play (launcher, library, cloud integration)
        |
        | spawn + line-delimited AYTHER_STATUS events
        v
AYTHER Runtime (this repository)
        |
        +-- AYTHER Engine: session, emulation, HD composition, audio
        +-- SDL3: window, keyboard, and gamepad input
        +-- Vulkan: swapchain, presentation, and post-processing
        +-- Dear ImGui: in-game overlay
        `-- user-supplied Libretro core and game content
```

Runtime is intentionally a single-session process. Process isolation keeps a
core or game-session failure outside the launcher while preserving an in-process,
zero-copy path between the engine frame view and the renderer.

## Current capabilities

- Runs a Libretro session from explicit core and ROM paths.
- Applies optional AYTHER packs and IPS/BPS patches in memory.
- Presents aspect-correct Vulkan output with selectable output profiles.
- Supports HD/original switching, rewind, fast-forward, subsystem controls,
  audio-bus controls, pack hot reload, diagnostics, and synchronized captures.
- Persists per-game/per-pack player settings and writes atomic save states.
- Exposes a line-oriented process protocol for AYTHER Play.
- Provides GPU-independent unit tests and a process-level core-probe test.

Runtime does **not** provide the game-library UI, downloads, account features,
or cloud synchronization. Those responsibilities belong to AYTHER Play.

## Repository layout

```text
.
|-- CMakeLists.txt                 build and runtime-asset staging
|-- dependencies/                  reproducible first-party dependency locks
|-- src/                           session host and presentation code
|   `-- vulkan_backend/            swapchain, present, and post-process path
|-- tests/                         CTest targets and process-contract tests
|-- tools/bootstrap_ayther_engine.ps1  verified Engine bootstrap and CMake prefix
|-- tools/fetch_ayther_engine.ps1      locked download/verification implementation
|-- tools/runtime_oot_smoke.ps1    published-package integration smoke test
|-- docs/                          architecture, protocol, status, and audits
|-- CHANGELOG.md                   notable project changes
|-- CONTRIBUTING.md                contribution and review requirements
|-- LICENSE                        Mozilla Public License 2.0 and project notice
`-- vcpkg.json                     pinned third-party dependency manifest
```

## Build from source

### Prerequisites

- CMake 3.21 or newer and a C++20 compiler. On Windows, the published
  `v0.1.0-rc.6` archive supports either Microsoft `cl` or LLVM `clang-cl` as
  the compiler frontend, but both must use the MSVC ABI, STL and linker from
  toolset v145 14.51 or newer (Visual Studio 2026). The archive was produced
  with `clang-cl` 20.1.8 over that toolset. MinGW/GNU is not compatible with
  the current Windows artifact.
- PowerShell 7 and an authenticated GitHub CLI (`gh`) for Engine provenance
  verification.
- A Vulkan-capable GPU, loader, and driver for interactive execution.
- The AYTHER package bootstrapped below, containing the `engine` component and
  its shaders (`Ayther::engine` and `Ayther_SHADER_DIR`).
- The `Ayther::engine` closure declared directly in `vcpkg.json`: SDL3, Vulkan,
  Vulkan Memory Allocator, toml++, and zstd.
- Runtime-owned direct dependencies: vk-bootstrap, Dear ImGui with SDL3/Vulkan
  backends, and stb.
- Ninja is recommended, but not required.

The exact supported Engine release and its platform artifacts are recorded in
`dependencies/ayther-engine.lock.json`. Validate the lock, or bootstrap the
default non-VPX artifact for the current platform, with:

```powershell
& ./tools/bootstrap_ayther_engine.ps1 -ValidateOnly
$enginePrefix = & ./tools/bootstrap_ayther_engine.ps1
```

Pass `-Variant engine-vpx` only when VP9 decoding is required. The downloader
never overwrites an existing archive or prefix. It validates the locked and
published checksums, verifies SLSA provenance against Engine's release workflow
and exact `rc.6` tag, extracts the package below `.deps/`, and returns its
absolute CMake prefix.

### Configure, build, and test

The following example uses vcpkg manifest mode:

```powershell
cmake -S . -B build -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  "-DCMAKE_PREFIX_PATH=$enginePrefix" `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo

cmake --build build
ctest --test-dir build --output-on-failure
```

During configuration, the vcpkg toolchain installs both Engine's native package
closure and Runtime's direct ImGui/stb dependencies from the pinned baseline.
`AytherConfig.cmake` remains responsible for resolving the imported Engine
targets; Runtime does not duplicate their `find_package` calls.

On Windows, configuration detects the compiler frontend and the underlying
Visual C++ toolset separately. It then compiles and links a small executable
against `Ayther::engine`; an unsupported frontend, a toolset older than 14.51,
or an ABI/link failure stops configuration with the detected values, the rc.6
requirement, and remediation guidance.

Runtime resolves stb directly through `find_package(Stb)` and links its local
header-only interface target. `capture.cpp` owns the PNG-writer implementation;
capture never obtains `stbi_write_png` from `Ayther::engine`.

Pull request CI uses the `windows-ci` and `linux-ci` presets from
`CMakePresets.json`. With `AYTHER_ENGINE_PREFIX` set to the bootstrapped Engine
prefix and `VCPKG_ROOT` set to the vcpkg installation, reproduce a job with:

```powershell
cmake --preset windows-ci
cmake --build --preset windows-ci
ctest --preset windows-ci --output-junit "$PWD/out/windows-ci-junit.xml"
```

Use `linux-ci` for the Linux/Ninja job. These are deliberately CI-only
RelWithDebInfo presets; the full Debug and analysis preset matrix belongs to
MAD-004.

For a multi-configuration generator, omit `CMAKE_BUILD_TYPE` and pass
`--config RelWithDebInfo` to the build and test commands. The executable and
staged runtime assets are written below `build/bin/`.

The out-of-tree smoke accepts either an installed Engine package prefix or a
release archive that Runtime verifies and extracts before configuring. It
derives the Runtime source root from its own location, so it can be invoked from
any working directory in an independent Runtime clone:

```powershell
& ./tools/runtime_oot_smoke.ps1 `
  -AytherPrefix C:/deps/ayther-engine `
  -ToolchainFile "$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

& ./tools/runtime_oot_smoke.ps1 `
  -EngineArchive C:/downloads/ayther-engine-v0.1.0-rc.6-windows-x86_64.zip `
  -ToolchainFile "$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
```

`-AytherPrefix` and `-EngineArchive` are mutually exclusive. If neither is
provided, the script downloads the artifact pinned by Runtime.

See [Development guide](docs/development.md) for build modes, test scope, shader
updates, CI branch-protection check names, and troubleshooting.

## Run

AYTHER Play normally launches Runtime. A direct development invocation is:

```powershell
build/bin/ayther_runtime.exe `
  --core C:/path/to/core.dll `
  --rom C:/path/to/game.rom `
  --pack C:/path/to/optional-pack.ay `
  --input-map C:/path/to/controls.toml
```

Probe a core without initializing SDL or loading a ROM:

```powershell
build/bin/ayther_runtime.exe --probe-core C:/path/to/core.dll
```

The positional form `ayther_runtime <core> <rom>` remains available for
backward compatibility, but new integrations should use named options. See the
[process and CLI contract](docs/process-protocol.md) before building a launcher
integration; only `AYTHER_STATUS <json>` lines are machine-readable protocol.

## Documentation

- [Architecture](docs/architecture.md) — boundaries, ownership, frame flow, and
  hard invariants.
- [Technical specification index](docs/specs.md) — normative document map and
  requirement language.
- [Process and CLI contract](docs/process-protocol.md) — options, events, exit
  behavior, and integration rules.
- [Runtime input map](docs/input-map.md) — TOML schema, defaults, validation,
  and compatibility behavior.
- [Development guide](docs/development.md) — local builds, tests, shaders, and
  debugging.
- [Project status](docs/status.md) — maturity, known gaps, and release gates.
- [Runtime/Engine dependency audit](docs/runtime-engine-dependency-audit.yaml) —
  detailed migration evidence and ownership decisions.
- [C++ code quality review](docs/code-quality-review.md) — prioritized
  correctness, ownership, architecture, hardcoding, and performance findings.
- [Changelog](CHANGELOG.md) — notable changes accumulated before the first
  supported release.

## Contributing, support, and security

Read [CONTRIBUTING.md](CONTRIBUTING.md) before proposing a change. General help
belongs in the channels described by [SUPPORT.md](SUPPORT.md). Do not disclose a
suspected vulnerability in a public issue; follow [SECURITY.md](SECURITY.md).

## Legal and content boundaries

- Unless a file states different terms, this repository's Runtime source code,
  headers, build modules, tests, tools, and source-form documentation are licensed
  under the [Mozilla Public License 2.0](LICENSE). MPL-2.0 applies at the file
  level and includes source-availability and notice obligations for distributions
  of Covered Software; consult the license text for the controlling terms.
- Third-party libraries, the AYTHER engine, Libretro cores, game images, patches,
  and packs remain subject to their own licenses and terms.
- The license does not grant rights to AYTHER names, trademarks, service marks,
  or logos except as strictly necessary to comply with license notices.
- Runtime dynamically loads a user-supplied Libretro core. AYTHER does not grant
  rights to that core or to game content, and users and distributors are
  responsible for ensuring that they have all required permissions.
- Runtime must not modify the source ROM or the supplied core. Patches are
  applied to an in-memory ROM buffer; AYTHER packs are the content-extension
  mechanism recognized by this component.
- The process boundary and bring-your-own-core design are architectural controls,
  not a legal opinion or a substitute for distribution-specific license review.
