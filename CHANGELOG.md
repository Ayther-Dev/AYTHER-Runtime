# Changelog

All notable changes to AYTHER Runtime will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project intends to follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html)
once stable compatibility guarantees are defined.

> [!WARNING]
> AYTHER Runtime is in early development. The build reports product version
> `0.1.0-beta.10`; this is an internal prerelease and is not supported for
> production use. The Runtime–Play process
> protocol v1 is the exception: its documented wire fields, reason identifiers,
> and exit codes are stable within v1.

## [Unreleased]

## [0.1.0-beta.10] - 2026-10-04

### Added

- `--probe-pack` lists the profiles the pack offers (`profiles`), and the
  checker preflight rejects a `--profile` the selected pack does not offer
  (`profile_not_in_pack: --profile`) before admitting the request.
- The checker preflight decompresses the initial state of every take whole, as
  the Runtime does before restoring it, and rejects a damaged state before
  admission (`take_initial_state_invalid: --take[i]`).
- `ayther_audio_qa options --format toml` publishes the commit of the sources
  of the build (`git` HEAD at build time, or `AYTHER_RUNTIME_SOURCE_COMMIT`).

### Changed

- `--profile` without a pack (or with `--pack-mode original`) is accepted with
  the same conditions as the run with the pack: the request shows and records
  the requested profile and `profile_effective=none`, and the Runtime only
  applies a profile to a loaded pack.
- The cadence of a visible take starts at the first present after its
  preparation, as after a resume, so the preparation and prewarm of the first
  frame no longer mark `cadence_degraded`. An isolated delay of more than one
  period still does.

### Fixed

- A visible inspection that played frames again after a step back could end
  with `runtime_evidence_stream_invalid` and no terminal: those frames were
  counted again among the presented or affected frames, and the terminal was
  rejected as malformed. Each take frame now counts once.
- `traversal.toml` records every stretch played between two inspection events,
  not only the one after the last event.
- `--language`, `--presentation` and `--pack-mode` given explicitly keep the
  `explicit` origin even when their value is the default.
- After a pause or a step, the debug overlay shows the current phase instead of
  the phase of the moment its frame was produced.
- An inspected take no longer ends with `evidence_error=pcm_continuity_failed`.
  Its evidence PCM is kept per linear segment (evidence 1.2): the Runtime opens
  a segment at every resume and tags each PCM block with it (PCM metadata 1.2,
  `segment`), and `traversal.toml` 1.2 declares the frames and samples of each
  segment in `audio_segments`. Continuity is audited within each segment; the
  jump between two segments left by a step forward or back is not a loss. A
  linear take still needs one continuous interval, and an inspection is never
  accredited as linear audio. Evidence 1.1 still reads: its PCM blocks are
  segment 0 and its traversal has no audio segments.
- A trust registry that is missing, unreadable or malformed (including a path
  pasted between quotes) is no longer reported as `pack_untrusted`:
  `--probe-pack` ends with 78 and `trust_registry_invalid`, and the checker
  rejects it in `--trust-registry`. `pack_untrusted` is kept for a pack the
  Engine refuses for its signature or trust policy; another failure to open is
  `pack_open_failed`.
- The replay QA launcher strips one pair of surrounding double quotes from a
  pasted path (Windows "Copy as path") in every path field; the summary and the
  request record the unquoted path.
- An inspected take no longer reports its evidence as incomplete (`data_lost`,
  `fragments_not_flushed`) because of the Engine facts its recoveries produce
  silently. After each recovery the Runtime declares, per Engine producer, the
  interval of sequences it excluded and its cause (`silent_recovery`) in a
  `fact_exclusion` fact, and `traversal.toml` 1.2 lists them in
  `fact_exclusions`. The trace audit takes a declared gap as an exclusion, not
  a loss, and a cause on an excluded fact as excluded; an undeclared gap is
  still a loss. An inspection whose only gaps are declared has complete
  evidence and is still never accredited as linear. Evidence 1.0, 1.1 and an
  earlier 1.2 still read; with nothing declared, their gaps stay losses.

## [0.1.0-beta.9] - 2026-10-04

### Added

- Replay inspection in the visible Runtime (spec 002): pause and resume with
  Space, one-frame steps forward and back while paused with recovery from
  checkpoints, the debug overlay with I, the protocol 1.1 live state and the
  evidence of every visit (`inspection_event`, `render_frame`).
- The replay QA launcher `ayther_replay_qa` in the `qa` component, with the
  checker preflight and runner of `ayther_audio_qa check`, effective values and
  material pinning.
- Render QA observation of each presented frame (contracts C3 and C4), including
  the frame start on the device output line (`audio_frame_output_boundary`).
- The `Windows / QA` pull request job, a required check of `main`, which builds
  the QA variant and runs `ctest --preset windows-qa -L audio_qa -LE gpu`.
- The QA input script retries a pause that did not land
  (`paused=<k> retry=<ms>`) instead of waiting forever, and the measurement
  program for plan §8 reports it.

### Changed

- The reproducible Engine lock moves from `v0.1.0-rc.13` to `v0.1.0-rc.15`.
- The QA Engine lock pins the published Windows `engine-vpx` artifact of
  `v0.1.0-rc.15` by URL, SHA-256, release `CHECKSUMS.sha256` and SLSA
  provenance; the QA bootstrap downloads and verifies it and no longer names a
  local path.
- After frame N−1 of an intermediate take, the next take starts on the next
  cadence slot (RF-2.8), so a pause requested in that wait keeps the take on its
  last frame. Each take boundary gains at most one period, outside the take
  time.

### Fixed

- A pause requested while no frame is in progress keeps the last completed
  frame instead of running the next one (RF-4.1); a frame being produced still
  finishes first, and resume runs the next frame.
- The QA session waits for the next frame while attending keys, so a pause is
  read within a millisecond instead of after the cadence wait.
- Linux builds compile every QA source and test without warnings under Clang
  `-Wextra`, and the inspection session test checks Engine facts per frame
  instead of device PCM, which varied between runs.

## [0.1.0-beta.8] - 2026-10-02

### Added

- RF-18 music continuity integration, including sequence state snapshots,
  transactional restoration, backend drain handling, discontinuity evidence,
  three-consumer semantics and reproducible audio QA tracing.

### Changed

- The reproducible Engine lock moves from `v0.1.0-rc.10` to
  `v0.1.0-rc.13`, with published SHA-256 values and SLSA provenance for all
  four supported packages.
- The secret-scanning action moves from TruffleHog `3.97.2` to `3.97.9` while
  remaining pinned to an immutable commit SHA.

### Fixed

- Linux quality and sanitizer builds now enforce clang-tidy consistently for
  every Audio QA target and compile all RF-18 test fixtures without warnings.

## [0.1.0-beta.7] - 2026-10-01

### Changed

- The Engine lock moves from `v0.1.0-rc.9` to `v0.1.0-rc.10`, which teaches the
  pose matcher the RELATIVE flip of each member sprite. Two poses that differ
  only in the flip of one member (a character whose head faces away from its
  body) are now told apart: when both match the same sprites, the one whose
  flips agree wins, a whole-pose mirror is still the same pose, and a tween
  between the two variants fires. In a left-right symmetric layout a mirrored
  instance now resolves to the right variant and is drawn mirrored. Packs
  already carried the per-member `flips`, so every existing pack plays as
  before except in those cases. The lock carries the published rc.10 checksums
  and SLSA provenance, and CI, the contract tests and the documentation follow
  the lock. Package version stays `0.1.0` and the C ABI revision stays at 7.

## [0.1.0-beta.6] - 2026-09-27

### Compatibilidad

- Reservas opcionales de ROM y pack para Play CE, con validación de revisiones y confirmación antes del arranque.
- Pruebas de bloqueo, liberación, rechazo y conservación del lanzamiento sin reservas. Engine permanece en rc.9; sin cambios de audio.


## [0.1.0-beta.5] - 2026-09-15

### Changed

- The Engine lock moves from `v0.1.0-rc.8` to `v0.1.0-rc.9`, which teaches the
  runtime to read a Sequence's SEGMENTATION STEP from the pack. Until now an
  `[[event]]` of `audio_events.toml` could only state its window, so the
  playing runtime segmented by that: a Sequence whose HD is longer than its
  musical phrase never re-anchored on its own period, and a phrase whose last
  note rings past the loop point swallowed the pass that starts there. The new
  `span` key is optional and its absence keeps the previous behaviour, so every
  pack built before it plays exactly as it did; the correction reaches a pack
  when its author re-exports it. The lock carries the published rc.9 checksums
  and SLSA provenance, and CI, the contract tests and the documentation follow
  the lock. Package version stays `0.1.0`, so the supported range
  `>=0.1.0,<0.2.0` is unaffected, and the C ABI revision stays at 7 because the
  new field reuses padding that was already in the struct.

## [0.1.0-beta.4] - 2026-09-12

### Changed

- The Engine lock moves from `v0.1.0-rc.6` to `v0.1.0-rc.8`: plane sets are
  tried by complexity, so a larger multi-tile element keeps its replacement
  when the one-tile elements it contains receive an asset, and a 1x1 plane
  sub under a glyph no longer claims the cell of another plane (rc.7). The
  lock carries the published rc.8 checksums and SLSA provenance; CI, the
  contract tests and the documentation follow the lock. The rc.6 reload
  path workaround is unchanged. Package version stays `0.1.0`, so the
  supported range `>=0.1.0,<0.2.0` is unaffected.

The beta.3 release and assets remain unchanged as historical reference.

## [0.1.0-beta.3] - 2026-09-09

### Added

- `--trust-registry <file.toml>` with pre-session path resolution and clear
  missing, unreadable and malformed configuration diagnostics.
- Deterministic test-only signed pack fixtures, CLI trust failure tests, and
  session reload/revocation coverage.
- Windows playback acceptance script requiring verified inputs, `has_pack=1`,
  600 frames, exit 0, and optional positive decoded VP9 frame count.

### Fixed

- Engine rc.6 receives the trust registry at creation and retains it for reloads.
  Runtime supplies an owned path on reload to avoid rc.6's path-aliasing bug.
- Explicit packs now fail with `pack.open_failed` and exit 4 instead of silently
  succeeding in original-ROM mode. Invalid registries exit 78.
- Windows release packages select the locked Engine VPX variant for VP9.

The beta.2 release and assets remain unchanged as historical reference.

## [0.1.0-beta.2] - 2026-09-04

### Added

- Startup-resolved `--input-map` TOML support for AYTHER Play keyboard and
  gamepad bindings, with partial-map defaults, strict ambiguity checks, stable
  `input.map_invalid` diagnostics, and no per-frame string parsing.

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

[Unreleased]: https://github.com/Ayther-Dev/AYTHER-Runtime/compare/v0.1.0-beta.2...HEAD
[0.1.0-beta.2]: https://github.com/Ayther-Dev/AYTHER-Runtime/releases/tag/v0.1.0-beta.2
[0.1.0-beta.1]: https://github.com/Ayther-Dev/AYTHER-Runtime/releases/tag/v0.1.0-beta.1
