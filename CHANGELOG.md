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

- Core probing now uses the installed Engine `CoreProbe` RAII facade and its
  JSON serialization; Runtime no longer includes Engine's unpublished dynamic
  loader or Libretro metadata types.
- Output-profile presets, filter selection, integer/fit scaling, and shader
  mixing now live entirely in Runtime under `ayther::runtime`; Runtime no
  longer consumes Engine's unpublished `output_profile.h`.
- Runtime now discovers and resolves player configuration, save-state, capture,
  and diagnostic paths through local `RuntimePaths`/`RuntimeConfig` types,
  without including Engine's unpublished `ayther_config.h` or initializing SDL.
- Engine shaders are now staged exclusively through the relocatable
  `Ayther_SHADER_DIR` package contract, which is validated during configure.
- Engine public headers now use their package-root `ayther/` paths, so Runtime
  builds with only the include directory exported by `Ayther::engine`.
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
- vcpkg manifest validation against the installed Engine package closure,
  including the pinned toml++ dependency and direct Runtime ownership of ImGui
  and stb.
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
- Focused CTest coverage for Runtime paths, player configuration, split
  geometry, capture metadata, pack layers, diagnostics, and the core-probe
  process contract.
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
