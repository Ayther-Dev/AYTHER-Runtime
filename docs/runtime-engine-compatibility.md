# Runtime–Engine compatibility contract

AYTHER Runtime `0.1.0-beta.15` supports the installed AYTHER Engine package
range `>=0.1.0,<0.2.0`. Reproducible builds use the narrower locked artifact
`v0.1.0-rc.23`, including its published checksum and SLSA provenance.

| Platform | Architecture | Compiler/ABI | C++ runtime |
| --- | --- | --- | --- |
| Windows | x86-64 | MSVC ABI, toolset v145 14.51 or newer within v145; `cl` or `clang-cl` | Dynamic MSVC/UCRT (`/MD`, `/MDd` for Debug) |
| Ubuntu 24.04 | x86-64 | Clang 18.x | libstdc++ |

Configuration rejects an absent version, a package below `0.1.0` or at/above
`0.2.0`, a non-x86-64 artifact, an unsupported operating system, or a compiler
outside the table. The diagnostic reports both the discovered values and the
required range, before Runtime links its main executable.

Runtime includes only installed `ayther/*.h` and `ayther/engine/*.hpp` headers
and links only the exported `Ayther::engine` target. No Engine source-tree,
private header, individual library path, or inferred package layout is part of
the contract.

## Optional native MVP pair (Windows x64)

The Tessera provider uses two exact software releases: an Engine VPX ZIP built
with the native provider, and `dependencies/tessera.native-mvp.lock.json`, which
pins the Tessera ZIP, its source commit, checksum manifest and attestation.
The regular `v0.1.0-rc.23` VPX package pinned by `dependencies/ayther-engine.lock.json`
does not carry the native provider: with it, detection under Tessera returns -6 and
the six `tessera_runtime_*` tests fail. The native route needs the Engine-only
`native-v0.1.0-rc.23` package (same headers; the libraries differ), pinned by
`dependencies/ayther-engine.native-mvp.lock.json` and prepared with
`./tools/bootstrap_ayther_engine.ps1 -LockFile dependencies/ayther-engine.native-mvp.lock.json -Variant engine-vpx`
(SHA-256, release manifest and `native-release.yml` provenance). The
`native_engine_lock_contract` test requires its tag to be `native-` plus the regular lock's tag.
Both native packages must be extracted and supplied in `CMAKE_PREFIX_PATH`.
Build with `AYTHER_ENABLE_TESSERA_NATIVE=ON` and
`AYTHER_ENGINE_LOCKED_RELEASE=native-v0.1.0-rc.23` so Runtime records the selected
Engine release. Verify the native ZIP against its release `CHECKSUMS.sha256` and
`gh attestation verify`; the Engine bootstrap (`-Variant engine-vpx`) prepares only
the regular package. The bootstrap verifies SHA-256, the release manifest and the signed workflow provenance.
Run `tools/verify_tessera_native_mvp.ps1 -ArchivePath <ZIP> -ChecksumsPath <CHECKSUMS.sha256>` before using the Tessera prefix; it checks the lock, ZIP, checksum manifest, source metadata, excluded fixtures and signed provenance.

The published `native-v0.1.0-rc.23` package passes the six `tessera_runtime_*` tests
locally. The downloaded draft ZIP pair was consumed by an out-of-tree executable and
Runtime's three local native tests, including GPU presentation and device audio.
The physical GPU/audio CI acceptance gate remains pending by user decision;
local passes do not satisfy that gate.