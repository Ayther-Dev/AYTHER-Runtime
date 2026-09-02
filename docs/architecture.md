# AYTHER Runtime Architecture

> [!WARNING]
> This document describes the current `0.1.0` implementation, not a stable
> public API. Package surfaces and ownership boundaries are still being narrowed.

## 1. Purpose and scope

`ayther_runtime` is the process that owns one game session. AYTHER Play starts it
when a user launches a game and observes it until the session exits. Runtime
keeps emulation failures outside the launcher while retaining an in-process,
zero-copy path for latency-sensitive frame data.

Runtime owns:

- session lifecycle and launcher-facing process events;
- SDL3 window, keyboard, and gamepad handling;
- Vulkan swapchain, presentation, aspect fitting, and CRT post-processing;
- the Dear ImGui in-game overlay;
- output-profile selection and per-game/per-pack player preferences;
- comparative captures, diagnostics, and save-state placement; and
- assembly of the pack layer stack used for presentation.

The AYTHER engine owns emulation, the Libretro host, session state, HD
substitution, audio processing, the offscreen renderer, and pack interpretation.
AYTHER Play owns the library UI, downloads, accounts, and cloud synchronization.

## 2. Process boundary

```text
+----------------------- AYTHER Play ------------------------+
| Launcher | library | session manifests | cloud integration |
+------------------------------+-----------------------------+
                               | spawn, arguments
                               | AYTHER_STATUS <json>
+------------------------------v-----------------------------+
|                     AYTHER Runtime                         |
|                                                           |
|  SDL input --> AytherSession::set_input()                  |
|                  |                                        |
|                  v                                        |
|           AytherSession::step() --> borrowed FrameView     |
|                  |                       |                 |
|                  |                       v                 |
|                  +--------------> AytherRenderer           |
|                                           | offscreen image|
|                                           v                |
|                          post-process / present / overlay   |
|                                           |                |
|                                           v                |
|                                      Vulkan swapchain      |
+-----------------------------------------------------------+
              |
              `-- dynamically loaded user-supplied core
```

The launcher treats an `exit` status event as a clean protocol completion. A
process termination without that event is an abnormal session failure. Human
logs may share stdout, so consumers must parse only complete lines beginning
with `AYTHER_STATUS `.

## 3. Hard invariants

1. **User-supplied core boundary.** Runtime dynamically loads a core selected by
   the user. The project does not distribute the core as part of Runtime. This
   is an architectural and distribution constraint, not legal advice; every
   distribution still requires its own license review.
2. **Content immutability.** Runtime does not modify the source ROM or core.
   Optional IPS/BPS patches are applied to an in-memory ROM buffer. AYTHER packs
   are the component's recognized content-extension mechanism.
3. **Frame-view lifetime.** `FrameView` is a borrowed, zero-copy view. Its
   pointers remain valid only until the next session mutation, including a step,
   rewind operation, or relevant reload.
4. **Single-threaded session driver.** The main emulation thread drives the
   session and renderer. Internally owned workers must stop and join before their
   resources are destroyed.
5. **Explicit Vulkan ownership.** Runtime owns presentation resources; the engine
   owns its offscreen renderer resources. Handles that cross the boundary are
   borrowed and must not be destroyed by the receiver.
6. **Single-purpose window.** The Runtime window contains gameplay and in-game
   controls only. Library management belongs to AYTHER Play.

## 4. Component map

| Path | Responsibility |
| --- | --- |
| `src/main.cpp` | CLI parsing, lifecycle, session loop, protocol events, persistence, and orchestration. |
| `src/game_input.*` | SDL keyboard/gamepad events to normalized RetroPad state. |
| `src/player_overlay.*` | In-game profile, subsystem, bus, shader, and HD controls. |
| `src/player_config.*` | Per-game/per-pack player settings and tolerant local persistence. |
| `src/capture.*` | Synchronized original/HD/comparison PNGs and non-content metadata. |
| `src/diagnostics.*` | Pure diagnostic decision logic and Markdown report generation. |
| `src/pack_layers.*` | Runtime-owned presentation stack derived from pack layers. |
| `src/vulkan_backend/vk_swapchain.*` | Swapchain and two-frames-in-flight synchronization. |
| `src/vulkan_backend/vk_present.*` | Aspect-correct blit, split presentation, and layout transitions. |
| `src/vulkan_backend/vk_postprocess.*` | CRT/post-process pipeline and fallback behavior. |
| `src/lab_interface.h` | Legacy no-op adapter scheduled for removal. |

## 5. Session lifecycle

```text
parse arguments
  -> optional core probe and immediate exit
  -> initialize SDL subsystems
  -> validate optional pack
  -> create AytherSession
  -> emit ready and now-playing events
  -> initialize Vulkan presentation when available
  -> apply persisted settings, then explicit launch overrides
  -> run frame loop
  -> serialize state to a temporary file and atomically rename
  -> tear down in dependency order
  -> emit exit event, optionally with the save-state path
```

An invalid or rejected pack must not prevent original gameplay. Runtime disables
pack derivation after an explicitly rejected pack so the engine cannot silently
load a different file by convention.

## 6. Frame and presentation flow

For each logical frame, Runtime applies current input and presentation settings
before stepping the session. The engine returns a `FrameView`; the renderer uses
that same logical frame to produce the offscreen image and any comparison image.

```cpp
session.set_input(input);
session.set_hd_enabled(hd_enabled);
FrameView frame = session.step();

renderer.render(vulkan, command_buffer, frame, pack, layer_stack);
postprocess_or_blit(renderer.framebuffer_image(), swapchain.image());
overlay.render(command_buffer, swapchain.image());
swapchain.end_frame();
```

The snippet is conceptual and intentionally omits error and fallback branches.

Presentation rules:

- Output is pillarboxed or letterboxed without stretching.
- Integer scaling falls back to fit when a 1x image cannot fit; it does not crop.
- Renderer images return to `SHADER_READ_ONLY_OPTIMAL` after transfer use.
- Swapchain images transition through transfer/render usage to `PRESENT_SRC_KHR`.
- Two frames are in flight. Per-filter descriptors prevent updates to resources
  still in use by an earlier frame.
- Missing post-process resources must degrade to a simple blit rather than make
  the session unusable.

Pacing follows the core timing, including non-integer rates such as 59.9227 Hz,
with audio dynamic-rate control used to maintain synchronization.

## 7. Configuration and precedence

Configuration is applied from least to most specific:

1. engine and pack defaults;
2. persisted per-game/per-pack player settings; and
3. explicit launch arguments from AYTHER Play or a developer.

Session-creation inputs such as the core path, ROM path, patch, and core options
are immutable after `AytherSession::create()`. Runtime controls such as output
profile, HD mode, subsystem mask, and bus state may change during the session.

The `--manifest` path is correlation metadata only. Runtime deliberately does
not parse it; AYTHER Play remains the single translator from a launch manifest
to process arguments.

`RuntimePaths` discovers Runtime's platform user-data root before core probing
and before SDL initialization. `RuntimeConfig` resolves the effective save root,
with a non-empty `--saves-dir` taking precedence. Player settings, captures,
diagnostics, and default saves are therefore Runtime-owned and do not require
an Engine configuration header or an Engine checkout.

## 8. Persistence and generated artifacts

- Player settings use a tolerant local TOML-like file keyed by sanitized game
  and optional pack identifiers. Unknown keys are ignored and malformed input
  falls back to defaults.
- Save states use a temporary file followed by rename. The launcher receives the
  final path only after a successful clean shutdown.
- Comparative capture writes original, HD, comparison PNGs, and metadata below
  Runtime's `capturas` directory. The metadata contract must not embed protected
  game pixels or other game content.
- Diagnostics are written as Markdown below Runtime's platform data directory.

## 9. Build boundary

Both standalone and parent-tree builds consume the engine in the same way:

```cmake
find_package(Ayther 0.1.0 CONFIG REQUIRED COMPONENTS engine)
target_link_libraries(ayther_runtime PRIVATE Ayther::engine)
```

The current `engine` surface is intentionally marked for narrowing. Runtime
still consumes broad engine headers for session, renderer, Vulkan interop,
Libretro probing, and pack operations. The detailed evidence and target
ownership decisions live in
[`runtime-engine-dependency-audit.yaml`](runtime-engine-dependency-audit.yaml).

## 10. Failure model

- Core probing returns distinct nonzero codes for load failure and missing
  Libretro symbols.
- Vulkan initialization or post-process failure should preserve a degraded
  execution path where possible.
- Pack validation errors disable the pack, not original gameplay.
- A failed resume preserves the supplied save file and starts a fresh session.
- Clean shutdown emits `exit`; missing `exit` means the launcher must classify
  the session as abnormal.

For exact process fields and current implementation limitations, see
[`process-protocol.md`](process-protocol.md) and [`status.md`](status.md).
