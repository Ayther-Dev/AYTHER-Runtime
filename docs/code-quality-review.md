# C++ Code Quality Review

**Review date:** 2026-09-01

**Status:** Early-stage engineering review; not a release certification

**Scope:** Runtime-owned C++ sources, headers, Vulkan presentation code, build
integration, and the directly related tests and tools

This review applies the [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
as engineering guidance rather than as a claim of formal conformance. Findings
are based on static inspection. Performance observations are explicitly marked
when they require profiling; no benchmark result is inferred from source code.

## Executive assessment

The code already has several sound foundations: the session and renderer use
RAII, pure geometry and policy functions are separated from Vulkan, frame timing
uses core-provided metadata, the split comparison is synchronized by design,
and descriptor sets are kept stable across in-flight frames. These choices align
with the Guidelines' emphasis on explicit interfaces, resource safety, and
testable policy.

The largest risk is concentration of lifecycle, protocol, persistence, input,
and rendering policy in a single 1,450-line `main` function. The most urgent
remaining concrete defects are permissive numeric parsing and unchecked Vulkan
and file-read results. The Vulkan frontend also relies on manual shutdown in
types whose destructors cannot enforce cleanup.

## Resolved since the review

- MIG-020 replaced `SDL_GetPrefPath` and Engine's unpublished
  `ayther_config.h` with Runtime-owned `RuntimePaths`/`RuntimeConfig`. Path
  discovery now occurs once before SDL initialization, so the SDL-owned string
  leak described by the original first high-priority finding no longer exists.

## Prioritized corrections

### High — fix before treating Runtime as integration-stable

1. **Replace `std::atoi` with validated, bounded parsing.**
   `src/main.cpp:138-139`, `:172`, and `:177` silently map malformed text to
   zero, accept trailing garbage, and provide no overflow signal. Negative masks
   are later converted to unsigned values. Introduce one `parse_integer` helper
   based on `std::from_chars`, return a structured error, validate each option's
   domain, and exit with a documented nonzero code on invalid input. Also make a
   missing option value terminate parsing instead of returning an empty string
   and continuing. This follows I.1, I.4, and ES.46.

2. **Make Vulkan failure paths transactional.**
   `VkPostProcess::init` (`src/vulkan_backend/vk_postprocess.cpp:410-417`) returns
   immediately after any failed stage without rolling back resources created by
   earlier stages. `PlayerOverlay` and `VkPostProcess` require an explicit
   context-dependent `shutdown`, while their default destructors cannot verify
   that the obligation was met. Introduce RAII wrappers for Vulkan handles with
   an explicit device owner, or use a local construction guard that commits only
   after full initialization. Keep `shutdown` idempotent for controlled teardown.
   This implements R.1 and C.30 instead of relying on call-site discipline.

3. **Check complete SPIR-V reads and every Vulkan result.**
   `src/vulkan_backend/vk_postprocess.cpp:40-69` ignores the return from
   `std::fread` and from `vkCreateShaderModule`. A truncated read can pass a
   partially zero-filled module to Vulkan, and module creation failure loses the
   actionable `VkResult`. Use an RAII file handle, reject short reads, and route
   all Vulkan results through one typed checker that retains the operation name
   and result code. Apply the same policy to waits, resets, and acquire/submit
   calls throughout the backend (E.5, E.19).

4. **Use one JSON serializer for every `AYTHER_STATUS` message.**
   The exit event at `src/main.cpp:1422-1429` replaces backslashes but does not
   escape quotes or control characters in a path. Other protocol fields are also
   formatted manually. A valid Windows path can therefore produce invalid
   line-delimited JSON. Centralize protocol encoding behind a small event model
   and the already-needed escaping policy; test quotes, newlines, non-ASCII text,
   and path separators. This is an interface invariant (I.1) and should not be
   duplicated at individual emission sites.

### Medium — address during the next structural iteration

5. **Decompose `main` by responsibility.**
   `src/main.cpp:43-1456` combines CLI parsing, core probing, SDL/Vulkan setup,
   pack validation, configuration precedence, the frame loop, capture,
   diagnostics, save-state persistence, IPC, and teardown. Extract cohesive
   units such as `RuntimeOptions`, `StatusEmitter`, `SessionFactory`,
   `PresentationController`, `CaptureService`, and `SaveStateStore`. Keep the
   top-level function as orchestration. This directly applies F.2, F.3, and P.4
   and makes failure paths independently testable.

6. **Replace the ad-hoc configuration parser or make its grammar exact.**
   `src/player_config.cpp:67-92` matches key prefixes rather than exact keys, so
   `hd_backup` is accepted as `hd`; invalid numbers and booleans silently become
   plausible values. Strings are written without escaping. Prefer the project's
   existing TOML dependency behind a Runtime-owned adapter. If that dependency
   is intentionally avoided, define an exact grammar, reject malformed fields,
   clamp gains, and return diagnostics separately from defaults (I.5, E.2).

7. **Unify bus cardinality and replace raw arrays.**
   `src/player_config.h:29-30` hardcodes four entries while consumers iterate to
   `ayther::kAudioBusCount` (`src/main.cpp:709-711`). A future Engine count change
   could cause an out-of-bounds access. Use `std::array<T, kAudioBusCount>` or a
   Runtime-owned serialized schema with an explicit conversion boundary. Add a
   compile-time assertion when ABI constraints require a fixed count (ES.42,
   ES.45).

8. **Harden capture size and commit semantics.**
   Pixel byte counts in `src/capture.cpp:44-45` and `:57-79` multiply dimensions
   without overflow checks. PNG failure removes images, but a metadata open or
   write failure at `:139-144` leaves all three PNGs behind despite the API's
   artifact-set semantics. Validate dimensions before allocation and integer
   narrowing, write all four outputs to temporary files, verify stream closure,
   then rename them into place as one best-effort commit. Return an error type
   rather than an empty string so callers can report the cause (I.12, E.2).

9. **Make swapchain access preconditions enforceable.**
    `src/vulkan_backend/vk_swapchain.h:70-80` exposes unchecked indexing into
    image and framebuffer vectors. Documented call order helps, but it does not
    prevent access before acquisition or after a failed rebuild. Represent the
    acquired frame as a short-lived object, or return checked views/spans and
    assert invariants in debug builds (I.6, Bounds.4).

10. **Resolved: remove the obsolete no-op plugin seam.**
    The unused adapter and its per-frame no-op calls were removed.
    Game-specific diagnostics now use the narrow, bounds-checked Runtime-owned
    `SonicTelemetry` value instead of FFI event structures. Any future plugin
    boundary must be defined from current requirements (I.2, I.23).

### Low — maintainability and consistency

11. **Move global Vulkan helpers into a Runtime namespace.** `FitRect`,
    `aspect_fit`, `VkPresent`, `VkPostProcess`, and `VkSwapchain` currently occupy
    the global namespace. A dedicated `ayther::runtime` namespace prevents symbol
    collisions and makes ownership visible (SF.20).

12. **Use typed parameter objects for rendering calls.** `PlayerOverlay::render`
    and `VkPostProcess::apply` have long lists of booleans, floats, and nullable
    pointers. Small value types such as `OverlayModel` and `PostProcessSettings`
    make units and valid ranges explicit and reduce argument-order mistakes
    (I.4, F.2).

13. **Replace historical comments with current invariants.** Issue references
    are useful traceability, but comments should lead with the rule the current
    code must preserve. Keep historical detail in architecture decisions or the
    changelog. Public header documentation has been revised in this pass to use
    contracts, ownership, preconditions, postconditions, and side effects.

## Hardcoding review

The following constants should be named and justified at their policy boundary:

- `320x240` in `src/main.cpp:336-337` and diagnostics is Genesis-specific. Read
  native geometry from the active core where possible; retain a named fallback
  only for unavailable metadata.
- `20-120 Hz` in `src/main.cpp:640-642` is a defensive pacing range. Move it into
  a documented timing policy and test corrupt, zero, PAL, NTSC, and high-refresh
  metadata.
- The `100 ms` pack-reload debounce (`src/main.cpp:887-891`) and `5%` split step
  (`:859-861`) are UX policy, not implementation details. Give them named
  constants and cover boundary behavior.
- Four audio buses in `PlayerConfig` are a schema decision and must not be an
  unrelated literal duplicated beside `kAudioBusCount`.
- Diagnostic thresholds (`src/diagnostics.cpp:26-27`) are product policy. Keep
  them named, explain the evidence behind them, and version changes that can
  alter recommendations.

Named constants are not automatically good design: values derived from core,
display, or Engine metadata should be derived rather than renamed. This is the
distinction intended by ES.45.

## Patterns to introduce

- **RAII handle wrappers:** SDL strings, `FILE*`, and Vulkan handles with explicit
  device-aware deleters.
- **Transactional builder/guard:** construct a complete swapchain, overlay, or
  post-process state and publish it only after every stage succeeds.
- **Command model plus validator:** parse CLI text into `RuntimeOptions`, then
  validate cross-field requirements before creating SDL or Vulkan state.
- **Adapter/facade boundary:** isolate `Ayther::engine`, C FFI, Libretro, and
  status-protocol details from Runtime policy.
- **State machine:** encode session states (`Starting`, `Ready`, `Running`,
  `Stopping`, `Failed`) so protocol events and legal transitions are testable.
- **Value objects:** strong types for frame counts, dimensions, normalized split
  positions, milliseconds, profile identifiers, and bit masks.

Patterns should be introduced only where they remove a demonstrated invalid
state or dependency. They are not goals by themselves.

## Performance review and measurement plan

Per.1 and Per.6 require evidence before optimization. The following are
candidates, not benchmark conclusions:

1. **Whole-device stalls:** resize, overlay rebuild, post-process rebuild, pack
   reload, and shutdown call `vkDeviceWaitIdle` (`src/main.cpp:900`,
   `src/player_overlay.cpp:204`, `src/vulkan_backend/vk_postprocess.cpp:427`).
   Shutdown is appropriate; interactive rebuilds may stall unrelated work.
   First measure resize and reload latency. If material, wait only on affected
   frame fences and defer destruction until GPU completion.

2. **Capture allocation and conversion:** a capture allocates original readback,
   AYTHER readback, split BGRA, and three RGBA vectors. This is outside the normal
   frame path, so clarity may be preferable. If burst capture is a requirement,
   profile allocation volume and reuse a capture workspace or convert directly
   into pre-sized outputs.

3. **Split comparison:** the second render per frame is intentional and paid
   only while comparison is enabled. Preserve that correctness invariant. Use
   GPU timestamp queries around both renders before considering shared
   intermediate work.

4. **Frame loop instrumentation:** retain the existing real-frame timing and add
   CPU phase timers plus Vulkan timestamp queries for upload, Engine rendering,
   post-process, overlay, and presentation. Report median, p95, and p99 over a
   fixed representative scene; averages alone hide hitching.

5. **Optimization gates:** define target platforms and budgets before changes.
   Validate with a release-with-debug-info build, Vulkan validation disabled for
   performance runs, and at least one integrated and one discrete GPU. Use
   RenderDoc for pass inspection and a sampling profiler or Tracy for CPU work.

## Recommended execution order

1. Fix SDL ownership, protocol escaping, numeric parsing, and unchecked reads.
2. Add focused tests for invalid CLI/config data and partial initialization.
3. Split `main` at protocol, persistence, and presentation boundaries.
4. Introduce transactional Vulkan construction and enforce acquired-frame state.
5. Establish repeatable CPU/GPU performance baselines.
6. Optimize only regressions that exceed the agreed frame or interaction budget.

## Verification limits

This review does not assert that the Runtime currently builds or runs in
isolation. The repository requires an installed `Ayther::engine` package and
GPU-dependent paths require compatible SDL/Vulkan drivers. Before release, run
the configured unit tests, the out-of-tree package smoke test, Vulkan validation
on lifecycle scenarios, and the performance matrix described above.

## Legal boundary

This engineering review does not change repository licensing or third-party
obligations. The MPL-2.0 file-level terms and the project boundaries documented
in `README.md`, `LICENSE`, and `docs/specs.md` remain controlling. The process
boundary around user-supplied Libretro cores is an architectural control, not a
legal opinion or a substitute for distribution-specific license review.
