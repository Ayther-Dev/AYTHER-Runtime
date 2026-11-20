# Project status

Last updated: 2026-11-20, for `0.1.0-beta.20`.

## Maturity

AYTHER Runtime `0.1.0-beta.20` is an internal prerelease. It is for
engineering integration and focused testing. It is not production-ready, not
generally available, and not backward compatible between betas.

| Surface | Status |
| --- | --- |
| Runtime–Play process protocol v1 ([contract](runtime-play-protocol.md)) | **Stable.** Status records, reason identifiers, negotiation and exit codes keep their meaning within v1; a breaking change needs a new protocol version. |
| Command-line options ([contract](process-protocol.md)) | Implemented; may change between betas. |
| Runtime/Engine package contract ([contract](runtime-engine-compatibility.md)) | Implemented for Engine `>=0.1.0,<0.2.0`; not stable. |
| Save-state and player-config formats | Implemented and versioned; no long-term compatibility policy. |
| End-user installer | Not present. Runtime ships as ZIP/TGZ archives that AYTHER Play CE consumes. |
| Third-party license inventory and notice bundle | Not present. |

## Release line

| Item | Value |
| --- | --- |
| Latest release | `v0.1.0-beta.20`, published 2026-11-20 ([notes](releases/v0.1.0-beta.20.md)) |
| Release assets | Windows x64 Runtime ZIP, Windows x64 QA Runtime ZIP, Linux x86-64 Runtime TGZ, one `SHA256SUMS-*.txt` per archive |
| Engine lock (release and QA) | `v0.1.0-rc.28` |
| Engine lock (Tessera route) | `native-v0.1.0-rc.28` with Tessera `v0.2.0-rc.9` |

Engine `v0.1.0-rc.29` exists but is not locked. The locks and how to bootstrap
them are described in [Runtime–Engine compatibility](runtime-engine-compatibility.md).

## Emulation providers

| Provider | Option | Status |
| --- | --- | --- |
| Libretro core supplied by the user | `--provider libretro` (default) with `--core` | Implemented in every release archive. |
| Tessera, AYTHER's own Mega Drive core, through Engine's `tessera_native` provider | `--provider tessera`, no `--core` | Implemented. The Windows release archive includes it from `v0.1.0-beta.19`; earlier releases do not. Linux archives do not. Mega Drive only. |

Save states record the emulator that wrote them; a state from the other
provider is refused (exit 65). See [Architecture](architecture.md#3-emulation-providers).

## Latest validation

These are the results recorded for the latest changes. They do not prove broad
GPU, driver, operating-system, core, or game compatibility.

| Date | Build | Result |
| --- | --- | --- |
| 2026-11-20 | `v0.1.0-beta.20`, `windows-ci`, Engine `v0.1.0-rc.28` | CTest 227/227. Adds `--accent-text`. |
| 2026-11-19 | `v0.1.0-beta.19` (PR #71), Engine `v0.1.0-rc.28` | `windows-ci` against the regular package: CTest 227/227. Against `native-v0.1.0-rc.28` + Tessera `v0.2.0-rc.9` with `AYTHER_REQUIRE_TESSERA_PROVIDER=ON`: CTest 227/227 (the `tessera_runtime_*` tests were not enabled). |
| 2026-11-19 | `v0.1.0-beta.18` (PR #70), `windows-ci`, Engine `v0.1.0-rc.28` | CTest 227/227 passed. Bootstrap verified SHA-256 and SLSA provenance for the regular and native locks. Release workflow published all six assets. |
| 2026-11-13 | `v0.1.0-beta.15`, Engine `v0.1.0-rc.23` | Regular package: 230/236 (the six `tessera_runtime_*` tests need the native package). Native package `native-v0.1.0-rc.23` with Tessera `v0.2.0-rc.9`: 236/236, including GPU presentation and device audio. |

Results of earlier betas are in the git history of this file and in the
[release notes](releases/). The Tessera integration tests and the `windows-qa`
and `windows-qa-gpu` suites were not rerun for beta.16 to beta.20.

Branch protection on `main` requires `Windows / RelWithDebInfo`,
`Linux / RelWithDebInfo`, `Linux / ASan-UBSan-Fuzz`, `Windows / QA`,
`Security / Dependency Review`, `Security / Secret Scan`, `Analyze (actions)`
and `Analyze (c-cpp)`. See
[Development guide](development.md#continuous-integration).

## Known gaps

1. `Ayther::engine` is still a broad, provisional package surface. Runtime uses
   only installed public headers, but the surface has not been narrowed.
2. Windows requires MSVC toolset v145 14.51 or newer; configuration rejects
   older toolsets. Linux is built and tested in CI with Clang on Ubuntu 24.04;
   it has no GPU validation.
3. GPU validation covers one discrete NVIDIA adapter on Windows 11. Integrated
   GPUs and Linux display stacks are untested. See the
   [compatibility matrix](compatibility-matrix-v1.md).
4. The Tessera integration tests (`tessera_runtime_*`) need locally generated
   fixtures and a GPU, so no CI job runs them. CI and the release only prove
   that the Windows build links the Tessera provider
   (`AYTHER_REQUIRE_TESSERA_PROVIDER=ON`).
5. Release archives are not code-signed. They carry the Runtime, Engine and
   (on Windows) Tessera licenses and notices under `share/licenses/`, but not a
   complete third-party inventory for the vcpkg-provided DLLs.

## Stable-release gates

A stable release needs, at minimum:

- MPL-2.0 notices and a complete third-party license inventory for every
  distributed artifact;
- a narrowed and versioned Runtime/Engine package contract;
- protocol v1 preserved, with any breaking wire change in a new version;
- declared supported platforms, compilers, Vulkan requirements and test cores;
- save-state and configuration migration policies; and
- Tessera integration tests running in CI.

This list defines release readiness. It is not a schedule.
