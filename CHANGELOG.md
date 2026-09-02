# Changelog

All notable changes to AYTHER Runtime will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project intends to follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html)
once stable compatibility guarantees are defined.

> [!WARNING]
> AYTHER Runtime is in early development. The build currently reports version
> `0.1.0`, but no supported release has been published from this repository.
> Until the first tagged release, entries remain under **Unreleased** and may
> describe interfaces that change without notice.

## [Unreleased]

### Fixed

- Runtime UI and startup logs now derive the Runtime version from CMake and the
  linked Engine version from `ayther::engine::version()` instead of embedding a
  stale hard-coded label.

### Added

- Reproducible AYTHER Engine `v0.1.0-rc.4` artifact lock for Linux and Windows
  x86_64, including standard and VPX variants, plus a bootstrap that verifies
  locked/published checksums and SLSA provenance, extracts the package, and
  returns its CMake prefix without requiring an Engine or monorepo checkout.
- Offline CTest coverage for the Engine lock schema and supported artifact
  matrix.
- Initial standalone Runtime repository structure for the C++20 game-session
  host consumed by AYTHER Play.
- CMake package consumption through
  `find_package(Ayther 0.1.0 CONFIG REQUIRED COMPONENTS engine)` and the
  `Ayther::engine` imported target.
- SDL3 input and window integration, Vulkan presentation and post-processing,
  Dear ImGui in-game controls, and committed SPIR-V runtime shaders.
- Libretro core probing and line-delimited `AYTHER_STATUS` process events.
- Per-game/per-pack player configuration, atomic save states, synchronized
  comparative captures, diagnostics, pack-layer composition, and pack hot reload.
- Focused CTest coverage for configuration, split geometry, capture metadata,
  pack layers, diagnostics, and the core-probe process contract.
- Project architecture, development, security, support, contribution, and
  release-readiness documentation.
- Mozilla Public License 2.0 coverage with a Runtime-specific project notice.
- Repository-wide line-ending, editor, generated-file, and binary-asset policies
  through `.gitattributes`, `.editorconfig`, and `.gitignore`.

### Security

- Documented that user-supplied Libretro cores execute as native code inside the
  Runtime process and are not sandboxed by the launcher process boundary.
- Documented handling requirements for untrusted cores, ROMs, packs, patches,
  manifests, paths, and save states.

[Unreleased]: https://github.com/Ayther-Dev/AYTHER-Runtime/commits/main
