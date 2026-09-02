# AYTHER Runtime

<img alt="AYTHER Runtime" height="128" src="docs/assets/branding/ayther-runtime-logo.svg" width="128"/>

AYTHER Runtime is the C++20 game-session host launched by AYTHER Play. It loads
a user-supplied Libretro core and game image, drives the AYTHER engine, renders
the original and HD-composed frame paths through Vulkan, and owns the in-game
presentation and controls.

> [!WARNING]
> **Early development:** version `0.1.0` is a pre-release engineering snapshot.
> APIs, command-line options, package boundaries, configuration formats, and
> saved-state compatibility may change without notice. There are no supported
> production releases or compatibility guarantees yet.

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
|-- tools/fetch_ayther_engine.ps1  locked Engine download and verification
|-- tools/runtime_oot_smoke.ps1    installed-package integration smoke test
|-- docs/                          architecture, protocol, status, and audits
|-- CHANGELOG.md                   notable project changes
|-- CONTRIBUTING.md                contribution and review requirements
|-- LICENSE                        Mozilla Public License 2.0 and project notice
`-- vcpkg.json                     pinned third-party dependency manifest
```

## Build from source

### Prerequisites

- CMake 3.21 or newer and a C++20 compiler.
- A Vulkan-capable GPU, loader, and driver for interactive execution.
- An installed AYTHER package containing the `engine` component and its
  shaders (`Ayther::engine` and `Ayther_SHADER_DIR`).
- The dependencies declared in `vcpkg.json`: SDL3, Vulkan, Vulkan Memory
  Allocator, vk-bootstrap, toml++, zstd, Dear ImGui, and stb.
- Ninja is recommended, but not required.

The AYTHER engine package is not included in this repository. A standalone
checkout therefore cannot configure until that package is available through
`CMAKE_PREFIX_PATH`.

The exact supported Engine release and its platform artifacts are recorded in
`dependencies/ayther-engine.lock.json`. Validate the lock, or download and
verify the default non-VPX artifact for the current platform, with:

```powershell
pwsh tools/fetch_ayther_engine.ps1 -ValidateOnly
pwsh tools/fetch_ayther_engine.ps1 -DestinationDirectory .deps
```

Pass `-Variant engine-vpx` only when VP9 decoding is required. The downloader
never overwrites an existing archive and rejects any SHA-256 mismatch.

### Configure, build, and test

The following example uses vcpkg manifest mode:

```powershell
cmake -S . -B build -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DCMAKE_PREFIX_PATH="C:/path/to/installed/ayther" `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo

cmake --build build
ctest --test-dir build --output-on-failure
```

For a multi-configuration generator, omit `CMAKE_BUILD_TYPE` and pass
`--config RelWithDebInfo` to the build and test commands. The executable and
staged runtime assets are written below `build/bin/`.

The out-of-tree smoke script is intended for the parent AYTHER monorepo. It
installs the engine package into a clean temporary prefix, builds Runtime
against that prefix, and runs its tests:

```powershell
pwsh runtime/tools/runtime_oot_smoke.ps1 -BuildDir build
```

See [Development guide](docs/development.md) for build modes, test scope, shader
updates, and troubleshooting.

## Run

AYTHER Play normally launches Runtime. A direct development invocation is:

```powershell
build/bin/ayther_runtime.exe `
  --core C:/path/to/core.dll `
  --rom C:/path/to/game.rom `
  --pack C:/path/to/optional-pack.ay
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
