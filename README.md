# AYTHER Runtime

<img alt="AYTHER Runtime" height="128" src="docs/assets/branding/ayther-runtime-logo.svg" width="128"/>

AYTHER Runtime is the C++20 game-session host that AYTHER Play CE launches for
each game. It runs the game through AYTHER Engine, with either a user-supplied
Libretro core or Tessera, AYTHER's own Mega Drive core. It presents the
original and HD-composed frames through Vulkan and owns input and the in-game
pause overlay.

> [!WARNING]
> **Beta.** Version `0.1.0-beta.21` is an internal prerelease. Command-line
> options, package boundaries, configuration and save-state formats may change
> between betas. Only the Runtime–Play process protocol v1 is stable. See
> [Project status](docs/status.md).

> [!IMPORTANT]
> Runtime source is licensed under the Mozilla Public License 2.0. Third-party
> dependencies, AYTHER products in other repositories, user-supplied cores and
> content, and AYTHER trademarks are outside that grant unless their rights
> holder states otherwise. See [Legal and content boundaries](#legal-and-content-boundaries).

## Download

Windows and Linux builds are published on the
[releases page](https://github.com/Ayther-Dev/AYTHER-Runtime/releases), each
with a SHA-256 checksum file. Releases are not code-signed: Windows may show
a SmartScreen warning, so check the archive against its checksum file.

## Role in the AYTHER system

```text
AYTHER Play CE (launcher, library)
        |
        | spawn + line-delimited AYTHER_STATUS events
        v
AYTHER Runtime (this repository)
        |
        +-- AYTHER Engine: session, emulation provider, HD composition, audio
        |     +-- libretro: user-supplied core
        |     `-- tessera_native: Tessera (Mega Drive)
        +-- SDL3: window, keyboard and gamepad input
        +-- Vulkan: swapchain, presentation and post-processing
        `-- Dear ImGui: in-game pause overlay
```

Runtime runs one session per process, so a core or session failure stays
outside the launcher, while frames still move zero-copy from Engine to the
renderer. Details are in [Architecture](docs/architecture.md).

## Capabilities

- Runs a game with a Libretro core (`--core`) or with Tessera
  (`--provider tessera`, Mega Drive, Windows x64; in the Windows release archive
  from `v0.1.0-beta.19`).
- Applies AYTHER packs and IPS/BPS patches in memory, and opens signed packs
  against an explicit [trust registry](docs/trust-registry.md).
- Presents aspect-correct Vulkan output with selectable output profiles.
- Switches HD and original, rewinds, controls subsystems and audio buses,
  hot-reloads packs, writes diagnostics and synchronized captures.
- Lets the launcher set the pause dialog's accent color, title and exit-button
  label.
- Persists per-game/per-pack player settings and writes atomic save states that
  record which emulator wrote them.
- Speaks a line-oriented process protocol with AYTHER Play CE.

Runtime has no game library, downloads or account features; those belong to
AYTHER Play CE.

## Quick start

Requirements, presets and options are in the
[Development guide](docs/development.md). On Windows, with Visual Studio 2026
(toolset v145), PowerShell 7, an authenticated GitHub CLI and `VCPKG_ROOT` set:

```powershell
# Download and verify the locked Engine package, then build and test
./tools/bootstrap_ayther_engine.ps1 -ValidateOnly
$env:AYTHER_ENGINE_PREFIX = & ./tools/bootstrap_ayther_engine.ps1 -Variant engine-vpx

cmake --preset windows-ci
cmake --build --preset windows-ci
ctest --preset windows-ci --output-on-failure
```

Run a game directly (AYTHER Play CE normally does this):

```powershell
$exe = "out/build/windows-ci/bin/RelWithDebInfo/ayther_runtime.exe"
& $exe --core <core.dll> --rom <game.rom> --pack <pack.ay>
& $exe --provider tessera --rom <game.md>     # needs a Tessera-route build
& $exe --probe-core <core.dll>                # inspect a core without a ROM
```

Building the Tessera route needs the native Engine package and Tessera; see
[Runtime–Engine compatibility](docs/runtime-engine-compatibility.md#tessera-route-windows-x64).
Every option is specified in the [process and CLI contract](docs/process-protocol.md).

## Repository layout

```text
.
|-- CMakeLists.txt, CMakePresets.json   build, staging, presets
|-- dependencies/                       Engine and Tessera locks
|-- src/                                session host and presentation
|   |-- vulkan_backend/                 swapchain, present, post-process
|   `-- replay_inspection/              replay QA inspection window
|-- tests/                              CTest targets and contract tests
|-- tools/                              bootstrap, smoke, QA tools
|-- docs/                               reference documentation
|   `-- releases/                       published release notes
|-- CHANGELOG.md
`-- vcpkg.json                          pinned third-party dependencies
```

## Documentation

| Document | For |
| --- | --- |
| [Architecture](docs/architecture.md) | Boundaries, providers, ownership, frame flow, invariants. |
| [Process and CLI contract](docs/process-protocol.md) | Options, status events, exit codes. |
| [Runtime–Play protocol](docs/runtime-play-protocol.md) | The stable protocol v1. |
| [Input map](docs/input-map.md) | Controls TOML schema. |
| [Trust registry](docs/trust-registry.md) | Signed-pack trust configuration. |
| [Development guide](docs/development.md) | Builds, presets, tests, CI, replay QA. |
| [Runtime–Engine compatibility](docs/runtime-engine-compatibility.md) | Engine range, locks, Tessera route. |
| [Runtime package layout](docs/packaging.md) | Installed tree and release archives. |
| [Project status](docs/status.md) | Maturity, latest validation, known gaps. |
| [Specification index](docs/specs.md) | Map of all normative documents. |
| [Changelog](CHANGELOG.md), [release notes](docs/releases/) | Changes per release. |

## Contributing, support and security

Read [CONTRIBUTING.md](CONTRIBUTING.md) before proposing a change. Questions
go through [SUPPORT.md](SUPPORT.md). Do not report a suspected vulnerability in
a public issue; follow [SECURITY.md](SECURITY.md).

## Legal and content boundaries

- Unless a file states other terms, this repository's source code, headers,
  build modules, tests, tools and documentation are licensed under the
  [Mozilla Public License 2.0](LICENSE); [NOTICE.md](NOTICE.md) states what it
  covers. MPL-2.0 applies per file and includes
  source-availability and notice obligations for distributions; the license
  text controls.
- Third-party libraries, AYTHER Engine, AYTHER Tessera, Libretro cores, game
  images, patches and packs are subject to their own licenses and terms.
- The license grants no rights to AYTHER names, trademarks, service marks or
  logos beyond what is needed to comply with license notices.
- With the libretro provider, Runtime dynamically loads a core that the user
  supplies. AYTHER grants no rights to that core or to game content; users and
  distributors are responsible for having the required permissions.
- Runtime never modifies the source ROM or the core. Patches apply to an
  in-memory copy; AYTHER packs are the only content-extension mechanism.
- The process boundary and the bring-your-own-core design are architectural
  controls, not a legal opinion or a substitute for license review of a
  distribution.
