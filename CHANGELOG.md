# Changelog

All notable changes to AYTHER Runtime will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project intends to follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html)
once stable compatibility guarantees are defined.

> [!WARNING]
> AYTHER Runtime is in early development. The build reports product version
> `0.1.0-beta.1`; this is an internal prerelease and is not supported for
> production use. The Runtime–Play process
> protocol v1 is the exception: its documented wire fields, reason identifiers,
> and exit codes are stable within v1.

## [Unreleased]

## [0.1.0-beta.1] - 2026-09-03

### Changed

- Declared the Runtime–Play process protocol v1 stable while keeping the
  Runtime product explicitly in the beta prerelease channel.
- Split the beta-hardening work into reviewable protocol, persistence, Vulkan,
  quality, packaging, and documentation commits.
- Advanced the verified AYTHER Engine dependency lock from `v0.1.0-rc.4` to
  `v0.1.0-rc.6`, pinning the official standard and VPX archives for Windows
  and Linux together with the published checksum manifest and release
  provenance.

### Fixed

- All process-status fields now pass through one typed JSON serializer, so
  quotes, backslashes, controls, line breaks, UTF-8, and save-state paths cannot
  corrupt or split a launcher protocol record.
- Numeric CLI options now use strict, typed `std::from_chars` parsing with
  explicit range/domain validation and process exit code `64` for malformed
  command lines.
- Runtime now checks every result-returning Vulkan/VMA operation across
  creation, synchronization, command recording, submission, presentation, and
  teardown while preserving the operation and symbolic/integer `VkResult` in
  diagnostics.
- Post-process shader loading now closes `FILE*` through RAII, rejects invalid
  sizes, and verifies that `fread` consumed the complete SPIR-V file.
- The standalone package smoke test now accepts either `-AytherPrefix` or
  `-EngineArchive` and derives the Runtime root from its own script directory,
  allowing it to run from an independent Runtime clone and any working
  directory.
- Runtime sources and tests now include the standard-library headers for every
  directly used type or facility instead of inheriting them from Engine, SDL,
  Vulkan, or another project header.
- Core probing now uses the installed Engine `CoreProbe` RAII facade and owned
  metadata; Runtime no longer includes Engine's unpublished dynamic loader or
  Libretro metadata types.
- SDL input mapping now produces the public Engine `InputState`/`JoypadButton`
  contract instead of including Engine's Libretro implementation header.
- Pack overlays and the renderer now compile through the installed Engine
  `PackOverlay`/`pack_overlays()` API and public `ayther/ayther_renderer.h`
  package path.
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

- Stable protocol negotiation, machine-readable error taxonomy and exit codes.
- Transactional save-state and capture boundaries, strict player configuration,
  Vulkan failure injection, a real validation-layer GPU smoke, clang-tidy,
  ASan/UBSan, coverage, deterministic fuzzing, and an installed-package smoke.
- Pull request CI for Windows and Linux using reproducible RelWithDebInfo CMake
  presets, the locked Engine `v0.1.0-rc.6` bootstrap, SHA-pinned GitHub Actions,
  least-privilege token permissions, and always-retained logs plus JUnit output.
- A CTest contract that guards the CI trigger, platform jobs, Engine lock,
  action pins, artifact retention, and CMake preset invariants.
- Typed `StatusEmitter` models for probe, ready, now-playing, warning,
  crash-test, and exit events, with real-parser and source-exclusivity tests.
- Deterministic shared-library fixtures for the successful and missing-symbol
  `probe_core` paths, replacing the optional external-core lock and validating
  exit codes plus complete metadata with CMake's JSON parser on every run.
- Configure-time Windows ABI enforcement for Engine `v0.1.0-rc.6`: Runtime
  accepts `cl` or `clang-cl` over MSVC v145 14.51+, rejects MinGW/GNU and
  older toolsets with actionable diagnostics, and verifies a real executable
  link against `Ayther::engine`.
- Injectable `VulkanCalls` dispatch and typed `VkFailure` results as the
  failure-testing seam for transactional Vulkan initialization.
- Reproducible AYTHER Engine artifact lock for Linux and Windows
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

[Unreleased]: https://github.com/Ayther-Dev/AYTHER-Runtime/compare/v0.1.0-beta.1...HEAD
[0.1.0-beta.1]: https://github.com/Ayther-Dev/AYTHER-Runtime/releases/tag/v0.1.0-beta.1
