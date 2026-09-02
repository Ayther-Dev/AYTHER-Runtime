# Process and CLI Contract

> [!WARNING]
> This is a pre-release contract. AYTHER Play and Runtime must be versioned and
> tested together until a stable protocol version is introduced.

## Invocation modes

Normal session:

```text
ayther_runtime --core <path> --rom <path> [options]
```

Core probe:

```text
ayther_runtime --probe-core <path>
```

The legacy positional form `ayther_runtime <core> <rom>` is accepted for
backward compatibility. New callers SHOULD use named options.

## Options

| Option | Value | Current behavior |
| --- | --- | --- |
| `--core` | path | Required core library for a normal session. |
| `--rom` | path | Required game image for a normal session. |
| `--pack` | path | Optional AYTHER pack. A rejected pack falls back to original gameplay. |
| `--patch` | path | Optional IPS/BPS patch applied to the in-memory ROM buffer. |
| `--profile` | id | Engine content profile selected before the first frame. |
| `--output` | id | Runtime presentation profile: `lcd`, `crt`, `pixel`, `smooth`, `cinema`, or `ntsc`. |
| `--subsystems` | integer mask | Explicit subsystem mask applied after persisted settings. |
| `--mute-buses` | integer mask | Explicit audio-bus mute mask. |
| `--core-option` | `key=value` | Repeatable core option applied during session creation. |
| `--shaders` | none | Explicitly enables shaders. |
| `--no-shaders` | none | Explicitly disables shaders. |
| `--manifest` | path | Correlation path logged and emitted in `ready`; Runtime does not parse the file. |
| `--saves-dir` | path | Save-state root chosen by AYTHER Play. |
| `--load-state` | path | Exact save state to resume; failure starts fresh and preserves the file. |
| `--rom-crc32` | hexadecimal text | ROM identity metadata included in save naming. |
| `--frames` | integer | Development/CI frame limit; `0` means unlimited. |
| `--capture-at` | comma-separated integers | Requests synchronized captures at logical frame numbers. |
| `--crash-test` | none | Emits `crash-test` and aborts; test-only launcher isolation hook. |
| `--probe-core` | path | Probes a core and exits without loading a ROM or initializing SDL. |
| `--hd-compose` | none | Deprecated compatibility option; accepted but no longer enables the removed path. |

Missing option values, unknown positional input, and numeric conversion are not
yet validated consistently. Callers MUST provide well-formed values and SHOULD
not treat permissive parsing as part of the stable contract.

## Status framing

Machine-readable events are emitted as one unbuffered stdout line:

```text
AYTHER_STATUS <JSON object>\n
```

Consumers MUST:

- parse only complete lines beginning with the exact `AYTHER_STATUS ` prefix;
- treat all other stdout and stderr as human diagnostic output;
- tolerate unknown event names and additional JSON fields; and
- treat process termination without an `exit` event as abnormal.

The protocol is not yet explicitly versioned. Fields documented below reflect
the current implementation.

## Events

### `probe`

Successful probe:

```json
{"event":"probe","ok":true,"api":1,"library_name":"...","library_version":"...","valid_extensions":"...","need_fullpath":false,"block_extract":false}
```

Failure reasons currently include `no_carga` (library load failure) and
`no_es_libretro` (required Libretro symbols missing). These reason tokens are
legacy wire values and MUST be treated as opaque identifiers.

Engine owns the temporary dynamic-library handle, copies the Libretro metadata,
and serializes it with JSON control-character escaping. Runtime adds only the
launcher-facing `event` and `ok` fields plus the `AYTHER_STATUS` framing.

### `ready`

```json
{"event":"ready","game_id":"...","has_pack":0,"manifest":"..."}
```

Indicates that the session exists. It does not guarantee that Vulkan
post-processing or every optional subsystem initialized successfully.

### `now-playing`

```json
{"event":"now-playing","game_id":"...","title":"..."}
```

Indicates transition into active gameplay. The current implementation uses the
game identifier as the title fallback.

### `warning`

```json
{"event":"warning","reason":"..."}
```

Reports a recoverable degradation, such as a loaded pack with no active
subsystems or a failed resume. The `reason` field is currently human-readable,
not a stable machine code.

### `crash-test`

Emitted immediately before the deliberate `--crash-test` abort. It is a test
hook and MUST NOT be interpreted as a clean exit.

### `exit`

```json
{"event":"exit"}
```

or, after a save state is written successfully:

```json
{"event":"exit","savestate":"C:\\path\\to\\state.bin"}
```

Only the `savestate` path from a clean `exit` is eligible for launcher-side
upload or synchronization.

## Core-probe exit codes

| Code | Meaning |
| ---: | --- |
| `0` | Core loaded and required Libretro information symbols were read. |
| `2` | Dynamic library could not be loaded. |
| `3` | Required Libretro symbols were not found. |

Other startup and runtime failures currently use general nonzero process codes.
Do not infer a stable taxonomy beyond the probe path.

## Security and trust

A Libretro core is native code loaded into the Runtime process. Probing reduces
launcher exposure but does not sandbox the core. Callers should treat cores,
ROMs, packs, patches, manifests, paths, and save states as untrusted input and
apply distribution-appropriate validation and provenance controls.
