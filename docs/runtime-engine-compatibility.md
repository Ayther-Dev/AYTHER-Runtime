# Runtime–Engine compatibility contract

This document states which AYTHER Engine packages Runtime builds against, how
they are pinned, and how to prepare the optional Tessera route. Build commands
are in the [Development guide](development.md).

## Supported package range

AYTHER Runtime `0.1.0-beta.18` accepts an installed AYTHER Engine package in
the range `>=0.1.0,<0.2.0` (`AYTHER_ENGINE_MIN_VERSION` and
`AYTHER_ENGINE_MAX_VERSION_EXCLUSIVE` in `CMakeLists.txt`).

| Platform | Architecture | Compiler and ABI | C++ runtime |
| --- | --- | --- | --- |
| Windows | x86-64 | MSVC ABI, toolset v145 14.51 or newer; `cl` or `clang-cl` | Dynamic MSVC/UCRT (`/MD`, `/MDd` for Debug) |
| Ubuntu 24.04 | x86-64 | Clang 18.x | libstdc++ |

Configuration rejects a missing package version, a version outside the range,
a non-x86-64 package, an unsupported operating system, or a compiler outside
the table, before the main executable is linked. On Windows it also links a
small probe executable against `Ayther::engine`
(`cmake/AytherWindowsToolchain.cmake`). Each diagnostic reports the detected
values and the requirement.

Runtime includes only installed `ayther/*.h` and `ayther/engine/*.hpp` headers
and links only the exported `Ayther::engine` target. Engine source trees,
private headers, individual library paths and inferred package layouts are not
part of the contract.

## Locked packages

Reproducible builds use exact Engine releases. Each lock pins the release tag,
the SHA-256 of every artifact and of the release `CHECKSUMS.sha256`, and the
expected SLSA provenance (signer workflow and source tag).

| Lock | Release | Artifacts | Used by | Bootstrap |
| --- | --- | --- | --- | --- |
| [`ayther-engine.lock.json`](../dependencies/ayther-engine.lock.json) | `v0.1.0-rc.28` | Windows and Linux x86-64, `engine` and `engine-vpx` | Pull request CI, release archives | `tools/bootstrap_ayther_engine.ps1` |
| [`ayther-engine.qa.lock.json`](../dependencies/ayther-engine.qa.lock.json) | `v0.1.0-rc.28` | Windows x86-64 `engine-vpx` | `windows-qa`, `windows-qa-gpu`, QA release archive | `tools/bootstrap_ayther_engine_qa.ps1` |
| [`ayther-engine.native-mvp.lock.json`](../dependencies/ayther-engine.native-mvp.lock.json) | `native-v0.1.0-rc.28` | Windows x86-64 `engine-vpx` with the Tessera provider | Tessera route (local builds) | `tools/bootstrap_ayther_engine.ps1 -LockFile dependencies/ayther-engine.native-mvp.lock.json -Variant engine-vpx` |
| [`tessera.native-mvp.lock.json`](../dependencies/tessera.native-mvp.lock.json) | Tessera `v0.2.0-rc.9` | Windows x86-64 | Tessera route (local builds) | Verified by `tools/verify_tessera_native_mvp.ps1` |

Engine `v0.1.0-rc.29` exists and is not locked.

The bootstraps need PowerShell 7 and an authenticated GitHub CLI (`gh`). They
fail if a file name, a locked or published SHA-256, the SLSA predicate, the
signer workflow or the source tag differs from the lock. They extract below
`.deps/`, reuse verified cache entries, never overwrite them, and print the
CMake prefix.

Lock contract tests (all run offline with `-ValidateOnly`):

| Test | Checks |
| --- | --- |
| `ayther_engine_lock` | Release lock identity, checksums, attestation policy and artifact matrix. |
| `native_engine_lock_contract` | The native lock is a single `native-release.yml` artifact and its tag is `native-` plus the release lock's tag. Bump both locks together. |
| `native_tessera_mvp_lock` | Tessera lock identity, URLs, checksums and attestation policy. |

## Tessera route (Windows x64)

The regular Engine package does not contain the `tessera_native` provider; a
Runtime built against it fails to start a `--provider tessera` session. The
Tessera route needs two packages built for each other: the native Engine
package and the Tessera package. Both prefixes must be on
`CMAKE_PREFIX_PATH`, because the native Engine package resolves Tessera with
`find_dependency`.

1. Prepare the native Engine package:

   ```powershell
   $enginePrefix = & ./tools/bootstrap_ayther_engine.ps1 `
     -LockFile dependencies/ayther-engine.native-mvp.lock.json `
     -Variant engine-vpx
   ```

2. Download the Tessera ZIP and its `CHECKSUMS.sha256` from the release in
   `tessera.native-mvp.lock.json`, then verify the lock, the archive, the
   checksum manifest, the source metadata, the excluded fixtures and the signed
   provenance:

   ```powershell
   ./tools/verify_tessera_native_mvp.ps1 -ArchivePath <tessera.zip> -ChecksumsPath <CHECKSUMS.sha256>
   ```

   Extract the verified archive yourself; the script does not extract it.

3. Configure with both prefixes and the native options:

   ```powershell
   cmake --preset windows-ci `
     "-DCMAKE_PREFIX_PATH=$enginePrefix;<tessera-prefix>" `
     -DAYTHER_ENGINE_LOCKED_RELEASE=native-v0.1.0-rc.28 `
     -DAYTHER_ENABLE_TESSERA_NATIVE=ON `
     -DAYTHER_TESSERA_TEST_FIXTURE_DIR=<fixtures>
   ```

   `AYTHER_ENGINE_LOCKED_RELEASE` records the native release in the package
   metadata. `AYTHER_ENABLE_TESSERA_NATIVE` adds the native QA driver and the
   `tessera_runtime_*` integration tests; configuration fails if the fixture
   directory does not contain `roms/fx03_rom.md`, `fx03/fx03.ay` and
   `fx02/program.md`. The fixtures are generated locally from Tessera and Engine
   and are never committed. The GPU and audio-device tests also need
   `AYTHER_ENABLE_GPU_TESTS=ON`.

The release workflow does not build this route, so the published archives do
not support `--provider tessera`. No CI job runs the Tessera integration
tests; they pass only in local runs (last recorded for beta.15, see
[Project status](status.md#latest-validation)).
