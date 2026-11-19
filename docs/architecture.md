# AYTHER Runtime architecture

> [!WARNING]
> This document describes the `0.1.0` beta implementation. It is not a stable
> public API; package surfaces and ownership boundaries are still being narrowed.

This document is for contributors who change Runtime and for integrators who
need to know what Runtime owns. Command-line options, status events and exit
codes are specified in the [process and CLI contract](process-protocol.md).

## 1. Purpose and scope

`ayther_runtime` is the process that owns one game session. AYTHER Play CE
starts it when the user launches a game and observes it until it exits.
Running each session in its own process keeps an emulation failure outside the
launcher, while frame data still moves in-process and zero-copy from Engine to
the renderer.

Runtime owns:

- the session lifecycle and the launcher-facing status events;
- the SDL3 window, keyboard and gamepad input;
- the Vulkan swapchain, presentation, aspect fitting and CRT post-processing;
- the Dear ImGui in-game pause overlay;
- output profiles and per-game/per-pack player preferences;
- comparison captures, diagnostics and save-state files; and
- the pack layer stack used for presentation.

AYTHER Engine owns emulation (through either provider, see
[section 3](#3-emulation-providers)), session state, HD substitution, audio
processing, the offscreen renderer and pack interpretation. AYTHER Play CE owns
the library UI, downloads and launch manifests.

## 2. Process boundary

```text
+------------------------- AYTHER Play CE -------------------------+
| library | launch manifests | process supervision                  |
+--------------------------------+---------------------------------+
                                 | spawn + arguments
                                 | AYTHER_STATUS <json> lines
+--------------------------------v---------------------------------+
|                         AYTHER Runtime                            |
|                                                                   |
|  SDL input --> AytherSession::set_input()                         |
|                     |                                             |
|                     v                                             |
|              AytherSession::step() --> borrowed FrameView         |
|                     |                         |                   |
|                     |                         v                   |
|                     +----------------> AytherRenderer            |
|                                               | offscreen image   |
|                                               v                   |
|                              post-process / present / overlay     |
|                                               |                   |
|                                               v                   |
|                                        Vulkan swapchain           |
+-------------------------------------------------------------------+
                     |
                     | AYTHER Engine emulation provider
                     +-- libretro: user-supplied core loaded at run time
                     `-- tessera_native: Tessera, linked into the native Engine package
```

The launcher treats an `exit` status event as a clean completion. A process
that terminates without it is an abnormal session failure. Human-readable logs
share stdout, so consumers parse only complete lines that begin with
`AYTHER_STATUS `.

## 3. Emulation providers

Engine runs the game through one of two providers. Runtime chooses the
provider with `--provider` and builds the matching `AytherSession::Config`; the
rest of the session (input, overlay, shaders, HD composition, capture, packs,
save states) is the same for both.

| | `libretro` (default) | `tessera` |
| --- | --- | --- |
| Engine provider | Libretro host | `EmulationProvider::tessera_native` |
| Core | User-supplied core, `--core <path>` | Tessera, AYTHER's own clean-room Mega Drive core. `--core` is rejected (`args.provider_conflict`, exit 64). |
| ROM delivery | Path passed to the core | Bytes read by Runtime and passed in `NativeConfig` (`src/native_session_config.cpp`) |
| Systems | Whatever the core supports | Mega Drive only. Another system fails before SDL starts with `core.unsupported_system` (exit 3). |
| Engine package | Regular package (`dependencies/ayther-engine.lock.json`) | Native package (`dependencies/ayther-engine.native-mvp.lock.json`) plus Tessera (`dependencies/tessera.native-mvp.lock.json`). The regular package does not contain the provider. |
| Platforms | Windows x64, Linux x86-64 | Windows x64 |

A pack baked against the libretro Genesis Plus GX core also plays under
Tessera.

**Emulator generation.** Engine reports an emulator generation (identifier,
version and determinism contract). Runtime adds it to the `ready` event and
writes it into every save state. Loading a state whose generation differs from
the running session fails with `state.generation_mismatch` (exit 65), because a
state from one emulator cannot restore the other. States written before the
generation was recorded count as Genesis Plus GX (`gpgx`, contract 1).

**Native QA driver.** In a build configured with
`AYTHER_ENABLE_TESSERA_NATIVE=ON`, the `--native-*` options with
`--provider tessera` select a separate, direct `NativeEmulation` driver
(`src/native_runtime.cpp`) for traces, input masks, deactivation tests, GPU and
audio-device probes. This driver is a development and QA tool, not the player
path. The `--native-*` options are rejected with any other provider.

## 4. Hard invariants

1. **User-supplied core boundary.** With the libretro provider, Runtime
   dynamically loads a core that the user selected. Runtime does not distribute
   that core. This is an architectural and distribution constraint, not legal
   advice; every distribution still needs its own license review.
2. **Content immutability.** Runtime does not modify the source ROM or the core.
   IPS/BPS patches are applied to an in-memory ROM buffer. AYTHER packs are the
   only content-extension mechanism.
3. **Frame-view lifetime.** `FrameView` is a borrowed, zero-copy view. Its
   pointers stay valid only until the next session mutation: a step, a rewind,
   or a reload.
4. **Single-threaded session driver.** The main thread drives the session and
   the renderer. Workers that Runtime owns stop and join before their resources
   are destroyed.
5. **Typed pack boundary.** Runtime gets the core ABI revision, pack metadata,
   validation findings, render tiers and reload events through the
   `ayther::engine` C++ API. `PackView` borrows from the session; `PackWatcher`
   owns its watcher. Runtime never includes or releases raw core FFI handles.
6. **Explicit Vulkan ownership.** Runtime owns the instance, surface, physical
   and logical devices, queues, VMA allocator, swapchain, presentation and UI
   resources. Engine owns only its offscreen renderer resources.
   `VulkanContextView` and `RenderImageView` borrow handles; neither side
   destroys the other side's resources.
7. **Single status boundary.** Only `StatusEmitter` writes process events. Every
   event uses its typed model, one JSON serializer, one complete-line write and
   an immediate flush. A source-contract test rejects framing elsewhere in
   `src/`.
8. **Checked Vulkan boundary.** Every Runtime-owned call that returns `VkResult`
   goes through `VulkanCalls` and is checked as a typed `VkFailure` that keeps
   the operation, symbolic result and integer code. Tests replace individual
   calls to exercise failure paths.
9. **Single-purpose window.** The Runtime window shows gameplay and in-game
   controls only. Library management belongs to AYTHER Play CE.

## 5. Component map

| Path | Responsibility |
| --- | --- |
| `src/main.cpp` | Entry point; calls `run_runtime_application`. |
| `src/runtime_application.*` | Process composition: startup order, session loop, persistence and teardown. |
| `src/runtime_options.*` | Strict, typed command-line parsing (exit 64 on error). |
| `src/runtime_error.h` | Stable reason identifiers and process exit codes. |
| `src/status_emitter.*` | Typed `AYTHER_STATUS` events and their JSON serialization. |
| `src/runtime_config.*` | Runtime-owned data, save and capture paths (`RuntimePaths`, `RuntimeConfig`). |
| `src/trust_registry.*` | Resolution of the pack trust registry passed to Engine ([trust registry](trust-registry.md)). |
| `src/file_reservation.h` | Optional ROM/pack reservations requested by Play ([reservations](specs/runtime-reservations.md)). |
| `src/native_session_config.*` | Tessera `NativeConfig` for a Mega Drive ROM. |
| `src/native_runtime.*` | Direct `NativeEmulation` QA driver; built only with `AYTHER_ENABLE_TESSERA_NATIVE`. |
| `src/session_controller.h` | Engine session boundary, independent of UI and presentation state. |
| `src/input_map.*` | Startup-only TOML validation of keyboard/gamepad bindings ([input map](input-map.md)). |
| `src/game_input.*` | Resolved SDL keyboard/gamepad state to RetroPad state. |
| `src/player_overlay.*`, `src/pause_dialog_options.h` | In-game pause dialog and controls; launcher-customizable accent, title and exit-button label. |
| `src/player_config.*` | Per-game/per-pack player settings and tolerant persistence. |
| `src/output_profile.h` | Runtime-owned output profiles (`lcd`, `crt`, `pixel`, `smooth`, `cinema`, `ntsc`). |
| `src/save_state_store.*` | Versioned, transactional save-state files. |
| `src/capture.*`, `src/capture_service.*` | Synchronized original/HD/comparison PNGs and metadata. |
| `src/diagnostics.*`, `src/audio_diagnostic.*` | Diagnostic decisions, Markdown reports, rate-limited audio diagnostics. |
| `src/pack_layers.*` | Presentation layer stack derived from pack layers. |
| `src/sonic_telemetry.*` | Bounds-checked, game-specific work-RAM diagnostics. |
| `src/presentation_controller.h` | Sole owner of the window-side Vulkan context, swapchain, post-process and overlay. |
| `src/vulkan_backend/` | Vulkan context, checked call dispatch, SPIR-V loading, swapchain, presentation, post-process. |
| `src/qa_*`, `src/audio_qa_*`, `src/replay_inspection/`, `src/music_*` | Replay QA session, inspection window and music-continuity logic; active only in QA builds with `--qa-session` ([audio QA evidence](audio-qa-evidence.md)). |

## 6. Session lifecycle

```text
parse arguments                                (error: exit 64)
  -> tessera + --native-* : run the native QA driver and exit
  -> --probe-core         : probe the core, emit `probe`, exit
  -> tessera              : read the ROM, build NativeConfig (non-Mega Drive: exit 3)
  -> optional file reservations, then AYTHER_RESERVATION 1
  -> resolve the trust registry, inspect the --pack file   (failure: exit 4)
  -> load the optional input map
  -> initialize SDL video, gamepad and audio
  -> initialize Vulkan presentation (missing Vulkan: warning, no GPU output)
  -> validate the pack                       (rejected --pack: exit 4)
  -> create AytherSession with the selected provider
  -> optional --load-state (generation mismatch: exit 65)
  -> emit `ready` and `now-playing`
  -> apply persisted settings, then explicit launch options
  -> run the frame loop
  -> write the save state to a temporary file and rename it
  -> tear down in dependency order
  -> emit `exit`, with the save-state path when one was written
```

A pack named with `--pack` is mandatory: if it cannot be opened, fails
validation, or Engine does not activate it, the launch ends with
`pack.open_failed` (exit 4) instead of silently running the original game. A
pack that Engine finds by naming convention is optional: if it is rejected,
Runtime emits a `pack.rejected` warning, continues in original mode, and
disables pack discovery so Engine cannot load a different pack instead.

## 7. Frame and presentation flow

For each frame, Runtime applies the current input and presentation settings,
steps the session, and renders the returned `FrameView`:

```cpp
session.set_input(input);
session.set_hd_enabled(hd_enabled);
FrameView frame = session.step();

renderer.render(vulkan.engine_view(), command_buffer, frame, pack, layer_stack);
ayther::engine::RenderImageView image = renderer.render_image();
postprocess_or_blit(image, swapchain.image());
overlay.render(command_buffer, swapchain.image());
swapchain.end_frame();
```

The snippet is conceptual and omits error and fallback branches.

Presentation rules:

- `ayther::runtime::OutputProfile` owns the output presets, including
  filtering, scaling and shader mixing. Engine supplies authored effect values
  and an optional recommended profile id; it knows no Runtime profile.
- Output is pillarboxed or letterboxed, never stretched.
- Integer scaling falls back to fit when a 1x image does not fit; it never crops.
- `RenderImageView` borrows Engine-owned image, view and sampler handles and
  declares their format, extent, handoff layout, access scope and queue family.
  Renderer images return to that layout after transfer use.
- Two frames are in flight. Per-filter descriptors prevent updates to resources
  that an earlier frame still uses.
- Missing post-process resources degrade to a plain blit.

Pacing follows the rate the core reports, including non-integer rates such as
59.9227 Hz, with dynamic audio-rate control keeping audio in sync. A provider
that reports its rate only after the first frame (Tessera) is re-paced as soon
as the rate is known.

## 8. Configuration and precedence

Configuration applies from least to most specific:

1. Engine and pack defaults;
2. persisted per-game/per-pack player settings;
3. explicit launch options from AYTHER Play CE or a developer.

Session-creation inputs (provider, core, ROM, patch, core options) are fixed
after `AytherSession::create()`. Output profile, HD mode, subsystem mask and
bus state can change during the session.

For the output profile, Runtime applies its LCD default, then a recognized pack
recommendation, then a recognized persisted or explicit user choice. Unknown
ids fall through without invalidating the session.

`--manifest` is correlation metadata only; Runtime does not parse it. AYTHER
Play CE is the single translator from a launch manifest to process arguments.

`--input-map` is parsed once, before SDL starts, because Runtime owns the input
contract. Partial maps inherit the compiled defaults; invalid or ambiguous maps
fail startup.

`RuntimePaths` finds Runtime's platform data root before core probing and SDL
initialization. `RuntimeConfig` resolves the save root; a non-empty
`--saves-dir` wins. Player settings, captures, diagnostics and default saves
are Runtime-owned and need no Engine configuration.

## 9. Persistence and generated files

| File | Format | Compatibility |
| --- | --- | --- |
| Player settings | Tolerant TOML-like file keyed by sanitized game and optional pack ids, `format_version = 1` | The legacy unversioned field set reads as version 0. Unknown keys are ignored; malformed input falls back to defaults. |
| Save state | `AYTHSTAT` envelope version 2: payload length, checksum and emulator generation | Version 1 and unwrapped files read as `gpgx / 1`. Unknown future versions and corrupt or truncated envelopes fail with `state.restore_failed`. |
| Capture | Original, HD and comparison PNGs plus metadata below Runtime's `capturas` directory | Metadata never embeds game pixels or other game content. |
| Diagnostics | Markdown below Runtime's data directory | Informational. |

Settings and save states are published through a temporary file and a rename.
Fault-injection tests show that storage exhaustion or interruption keeps the
previous valid file. The launcher receives the save-state path only after a
clean shutdown.

`PresentationController` releases overlay, post-process, swapchain and context
in that order from its destructor, so normal returns and partial startup
failures need no manual presentation shutdown. Engine's renderer borrows the
context and is released first.

## 10. Build boundary

Runtime consumes Engine only as an installed CMake package:

```cmake
find_package(Ayther 0.1.0...<0.2.0 CONFIG REQUIRED COMPONENTS engine)
target_link_libraries(ayther_runtime PRIVATE Ayther::engine)
```

Runtime includes only installed public headers. The `engine` surface is still
broad (session, renderer, Vulkan interop, input constants, pack operations) and
is marked for narrowing; core probing already goes through the public
`ayther::engine::CoreProbe` contract. The evidence and ownership decisions are
in [`runtime-engine-dependency-audit.yaml`](runtime-engine-dependency-audit.yaml);
the supported versions and locks are in
[Runtime–Engine compatibility](runtime-engine-compatibility.md).

## 11. Failure model

- Core probing returns distinct non-zero codes for load failure, missing
  Libretro symbols and other probe verdicts (Engine probe contract v2).
- Vulkan initialization or post-process failure keeps a degraded path where
  possible.
- A rejected `--pack` ends the launch (exit 4); a rejected convention-found
  pack only disables HD substitution.
- A failed resume keeps the supplied save file. A state from another emulator
  generation ends the launch with exit 65.
- Clean shutdown emits `exit`; a missing `exit` means the launcher classifies
  the session as abnormal.

Exact event fields, reason identifiers and exit codes are in
[`process-protocol.md`](process-protocol.md) and
[`runtime-play-protocol.md`](runtime-play-protocol.md).
