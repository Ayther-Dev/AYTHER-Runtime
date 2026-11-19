# Command line and status events

This is the reference for the `ayther_runtime` command line and the
`AYTHER_STATUS` events it emits. It is written for launcher authors (AYTHER
Play CE) and for developers who start Runtime by hand.

The compatibility rules of the wire protocol (framing, negotiation, stable exit
codes and reason identifiers) are normative in
[`runtime-play-protocol.md`](runtime-play-protocol.md). This page does not
repeat them.

Source of truth: `src/runtime_options.h` and `src/runtime_options.cpp` (parser),
`src/runtime_application.cpp` (dispatch, usage text, exit paths),
`src/runtime_error.h` (exit codes and reasons), `src/status_emitter.cpp`
(event serialization).

## Invocation modes

| Mode | Command | Notes |
| --- | --- | --- |
| Libretro session | `ayther_runtime --core <core> --rom <rom> [options]` | Default provider (`libretro`). |
| Tessera session | `ayther_runtime --provider tessera --rom <rom> [options]` | Native Mega Drive provider. No `--core`. |
| Core probe | `ayther_runtime --probe-core <core> [--probe-rom <rom>]` | Exits without SDL initialization. |
| Pack probe | `ayther_runtime --probe-pack <pack> [--trust-registry <file>]` | QA build only. |
| Tessera diagnostic run | `ayther_runtime --provider tessera --rom <rom> --native-... ` | Selected by any `--native-*` option. Diagnostic; not a player session. |
| QA capability query / session | `--qa-capabilities`, `--qa-session ...` | Started by `ayther_audio_qa`; see [tools/audio_qa](../tools/audio_qa/README.md). |

Positional form: arguments that are not recognized options are collected as
positional values. With the libretro provider the first is the core and the
second the ROM (`ayther_runtime <core> <rom>`); with `--provider tessera` the
first is the ROM. The form remains for backward compatibility; new callers
SHOULD use named options.

Dispatch order: the Tessera diagnostic run, `--qa-capabilities`, `--qa-session`,
`--probe-pack`, protocol negotiation, `--probe-core`, then the session.

## Options

Every option accepted by the parser appears exactly once below.

### Session

| Option | Value | Behavior |
| --- | --- | --- |
| `--provider` | `libretro` or `tessera` | Emulation provider. Default `libretro`. Any other value is `value_out_of_domain` (exit 64). `tessera` requires an Engine package built with Tessera; it emulates Mega Drive only (another system: `core.unsupported_system`, exit 3; Engine without Tessera: `core.load_failed`, exit 2). |
| `--core` | path | Libretro core. Required with the libretro provider. With `--provider tessera` it is rejected with `args.provider_conflict` (exit 64). |
| `--rom` | path | Game image. Required for a session. |
| `--pack` | path | Explicit AYTHER pack. Must not be empty. If it cannot be opened, validated, activated or reloaded, Runtime exits 4 (`pack.open_failed`). |
| `--trust-registry` | path | Public-key TOML registry passed to Engine. Must not be empty. See [trust-registry.md](trust-registry.md). |
| `--patch` | path | IPS/BPS patch applied to the in-memory ROM buffer, never to the file. |
| `--profile` | id | Engine content profile selected before the first frame. An id the pack does not offer is reported on stderr and the pack's default profile is used. |
| `--output` | id | Presentation profile: `lcd` (default), `crt`, `pixel`, `smooth`, `cinema` or `ntsc`. Overrides the stored player preference. |
| `--subsystems` | unsigned decimal `uint32_t` | Subsystem mask applied after persisted settings. |
| `--mute-buses` | unsigned decimal `uint32_t` | Audio-bus mute mask. |
| `--core-option` | `key=value` | Repeatable. Applied when the core initializes. The key must not be empty (`malformed_core_option`). |
| `--shaders` | none | Enables shaders. |
| `--no-shaders` | none | Disables shaders. |
| `--input-map` | path | Keyboard/gamepad TOML map, read once before SDL initialization. Must not be empty. See [input-map.md](input-map.md). |
| `--saves-dir` | path | Save-state root chosen by the launcher. |
| `--load-state` | path | Save state to resume. If it cannot be read or restored, the session starts from power-on, emits `state.restore_failed` and keeps the file. A state written by another emulator generation stops the launch with `state.generation_mismatch` (exit 65). |
| `--rom-crc32` | hexadecimal text | ROM identity used in save-state naming. |
| `--manifest` | path | Correlation path logged and echoed in `ready`. Runtime does not read the file. |

### Launcher integration

| Option | Value | Behavior |
| --- | --- | --- |
| `--play-protocol-version` | unsigned decimal `uint32_t` | Protocol negotiation before SDL, Vulkan, core, ROM or session startup. Only `1` is accepted. See [negotiation](runtime-play-protocol.md#negotiation). |
| `--rom-revision` | token | Windows file reservation for the ROM. Must not be empty. See [file reservations](runtime-play-protocol.md#optional-file-reservations). |
| `--pack-revision` | token | Windows file reservation for the pack. Must not be empty. |
| `--accent-color` | `#RRGGBB` (`#` optional, hex digits in any case) | Pause-dialog accent: caption, primary button, checkboxes, sliders and hover states. Default `#FF7300`. Any other spelling is `value_out_of_domain`. |
| `--title` | text | Caption shown above «EN PAUSA» in the pause dialog. Default `Runtime`. Must not be empty. |
| `--exit-button-text` | text | Label of the pause-dialog button that closes Runtime. Default `Salir`. Must not be empty. |

### Development and CI

| Option | Value | Behavior |
| --- | --- | --- |
| `--frames` | unsigned decimal `uint64_t` | Clean exit after N emulated frames. `0` means unlimited. |
| `--capture-at` | comma-separated positive `uint64_t` | Repeatable. Requests a capture (the F12 path) at each listed frame. |
| `--input-script` | path | A pad timeline (the grammar of [`--timeline`](../tools/audio_qa/README.md#pad-timelines)) drives port 0 from power-on instead of keyboard and gamepad; the run ends after its last frame. Only `start power_on` is accepted. A missing or invalid script exits 64. Works with both providers. |
| `--frame-log` | path | Writes one CSV row per displayed frame: `frame,t_ms,step_ms,work_ms,interval_ms,pacing_hz,tile_ms,sprite_ms,audio_ms`. If the file cannot be opened Runtime exits 64. |
| `--crash-test` | none | Emits `crash-test` and aborts. Test hook for launcher crash handling. |
| `--hd-compose` | none | Retired. Accepted for compatibility; prints a notice and has no effect. |

### Probes

| Option | Value | Behavior |
| --- | --- | --- |
| `--probe-core` | path | Probes a libretro core with the Engine probe contract v2, emits one `probe` event and exits. No ROM, no SDL. |
| `--probe-rom` | path | With `--probe-core`: also loads this ROM in a session without running a frame and reports `game_*` fields. `--patch` and `--core-option` apply. |
| `--probe-pack` | path | QA build only. Prints one `AYTHER_PACK_PROBE <json>` line and exits 0 (usable), 66 (unusable) or 78 (`trust_registry_invalid`). A non-QA build prints `"status":"unavailable"` and exits 65. |

### Tessera diagnostic run

Diagnostic options. Any of them, together with `--provider tessera`, selects
the direct native driver (`src/native_runtime.cpp`) instead of a player
session; it is compiled only with `AYTHER_ENABLE_TESSERA_NATIVE`. The run needs
`--rom` and either `--frames` or `--native-trace`, rejects every libretro or
session option (`--core`, `--patch`, `--core-option`, `--input-map`,
`--load-state`, `--profile`, `--subsystems`, `--mute-buses`, shader options,
`--output`, `--capture-at`, `--manifest`, `--hd-compose`) with exit 64, and
prints `AYTHER_NATIVE_*` lines that are not protocol records. Without
`--provider tessera` these options fail with exit 64.

| Option | Value | Behavior |
| --- | --- | --- |
| `--native-trace` | path | Replays a Tessera input trace. Must not be empty. |
| `--native-input-mask` | unsigned decimal, `0..255` | Holds pad-1 controls every frame (bit *n* = control *n*). |
| `--native-deactivate-at` | positive `uint64_t` | Frame at which the HD presentation policy is switched off. |
| `--native-reactivate-at` | positive `uint64_t` | Frame at which it is switched back on. Must be greater than `--native-deactivate-at`. |
| `--native-visible` | none | Opens a window and presents frames. |
| `--native-audio-device` | none | Opens the SDL audio device. |
| `--native-gpu-probe` | none | Hashes presented GPU frames (`AYTHER_NATIVE_GPU_FRAME`). |

### QA session

Set by `ayther_audio_qa`; not meant to be typed by hand. They take effect in a
QA build (`windows-qa` preset). See [tools/audio_qa](../tools/audio_qa/README.md).

| Option | Value | Behavior |
| --- | --- | --- |
| `--qa-capabilities` | none | Prints one `AYTHER_QA_CAPABILITIES <report>` line and exits 0. |
| `--qa-session` | none | Runs a supervised replay session. A non-QA build prints `AYTHER_QA_SESSION` with `"status":"unavailable"` and exits 65. |
| `--qa-control-channel` | handle token | Inherited control channel. |
| `--qa-data-channel` | handle token | Inherited data channel. |
| `--qa-run-id` | id | Run identity of the take. |
| `--qa-presentation` | `none` (default) or `visible` | Headless or visible replay. |
| `--qa-take-position` | unsigned decimal `uint32_t` | Position of the take in the request. |
| `--qa-last-take` | none | Marks the last take of the request. |
| `--qa-close-at-end` | none | Closes the window after the confirmed natural end. |

### Value validation

- An option that takes a value fails with `missing_value` when the value is
  absent or the next argument starts with `--`.
- Numbers are unsigned decimal with complete-input and range checks. Signs,
  overflow, trailing text and empty values are rejected.
- `--capture-at` rejects zero, empty elements and leading, trailing or repeated
  commas.
- Every parse error emits a `warning` with `cli.invalid_argument` (or
  `args.provider_conflict`), writes a diagnostic to stderr and exits 64 before
  SDL initialization.

## Status events

Each event is one line, `AYTHER_STATUS <JSON object>`, framed as described in
[status framing](runtime-play-protocol.md#status-framing). Fields are listed in
emission order. Consumers ignore unknown fields and events.

### `probe`

`--probe-core` follows the Engine core probe contract v2. The record is the
Engine's `ProbeReport::serialize()`, so it carries `"protocol_version":2` (the
probe contract version, not protocol v1), `ok`, `verdict`, `reason` (absent
when `ok`), `architecture`, `ayther_abi`, `ayther_build_id`, `capabilities`,
`required_capabilities` and `missing_capabilities`.

```json
{"protocol_version":2,"event":"probe","ok":true,"verdict":"ok","api":1,"library_name":"...","library_version":"...","architecture":"x64","ayther_abi":"1.10","ayther_build_id":"...","capabilities":"0xEFFFF","required_capabilities":"0xEFFFF","missing_capabilities":[],"loaded":true,"valid_extensions":"...","need_fullpath":false,"block_extract":false}
```

Runtime appends only:

- `loaded`: the libretro entry points answered, even when the verdict is not `ok`;
- `valid_extensions`, `need_fullpath`, `block_extract`: when `loaded`;
- with `--probe-rom`: `game_loaded`; when loaded, `timing_fps` (if positive),
  `geometry_known` and, when known, `geometry_width` and `geometry_height`;
  when not loaded, `game_message`.

Reasons are stable identifiers: `core.missing`, `core.wrong_architecture`,
`core.invalid`, `core.abi_missing`, `core.capabilities_incomplete`. `message`
is human text and MUST NOT drive logic.

Exit codes are the probe contract's: `0` ok, `2` missing file or wrong
architecture, `3` not a core, `4` ABI missing or capabilities incomplete. A
supervising parent may add `5` (timeout) and `6` (crashed).

### `ready`

```json
{"protocol_version":1,"event":"ready","game_id":"...","has_pack":false,"manifest":"..."}
```

The session exists. It does not guarantee that Vulkan post-processing or every
optional subsystem initialized.

When Engine reports the emulator generation of the session, three fields follow
`manifest`; they are absent otherwise:

```json
{"protocol_version":1,"event":"ready","game_id":"...","has_pack":false,"manifest":"...","emulator_id":"gpgx","emulator_version":"...","determinism_contract":1}
```

- `emulator_id`: `gpgx` for Genesis Plus GX, the slug of another libretro
  core's name, or `tessera` for the native provider.
- `emulator_version`: core version or backend build id.
- `determinism_contract`: number.

Save states record the same generation; see
[`state.generation_mismatch`](runtime-play-protocol.md#error-taxonomy).

### `now-playing`

```json
{"protocol_version":1,"event":"now-playing","game_id":"...","title":"..."}
```

Gameplay is active. `title` currently falls back to the game identifier.

### `warning`

```json
{"protocol_version":1,"event":"warning","reason":"persistence.config_invalid","message":"..."}
```

A typed diagnostic. `reason` is a stable token from the
[error taxonomy](runtime-play-protocol.md#error-taxonomy); `message` is human
text. A `warning` can precede a fatal exit: use the process exit code, not the
event name, to decide recoverability.

### `crash-test`

Emitted immediately before the `--crash-test` abort. It is not a clean exit.

### `exit`

```json
{"protocol_version":1,"event":"exit"}
```

or, after a save state was written:

```json
{"protocol_version":1,"event":"exit","savestate":"<saves-dir>\\<state>.bin"}
```

Only the `savestate` path of a clean `exit` is eligible for launcher-side upload
or synchronization.

## Other stdout lines

These lines are not `AYTHER_STATUS` records:

| Line | Emitted by |
| --- | --- |
| `AYTHER_RESERVATION 1` | Reservations acquired (`--rom-revision` / `--pack-revision`). |
| `AYTHER_PACK_PROBE <json>` | `--probe-pack`. |
| `AYTHER_QA_CAPABILITIES <report>` | `--qa-capabilities`. |
| `AYTHER_QA_SESSION <json>` | `--qa-session` in a non-QA build. |
| `AYTHER_NATIVE_<KIND> ...` | Tessera diagnostic run. |
| `[tag] text` | Human diagnostics; never parse. |

## Security

A libretro core is native code loaded into the Runtime process. Probing reduces
launcher exposure but does not sandbox the core. Treat cores, ROMs, packs,
patches, manifests, paths and save states as untrusted input and apply
distribution-appropriate validation and provenance controls.
