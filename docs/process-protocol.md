# Process and CLI Contract (protocol v1)

> [!IMPORTANT]
> The Runtime–Play wire protocol v1 is stable. AYTHER Runtime remains the
> separate `0.1.0-beta.1` product; stability here does not extend to its
> package, Engine ABI, configuration, or save-state formats.

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
| `--input-map` | path | Optional TOML keyboard/gamepad map, parsed once before SDL initialization. Omitted entries inherit Runtime defaults. |
| `--profile` | id | Engine content profile selected before the first frame. |
| `--output` | id | Runtime presentation profile: `lcd`, `crt`, `pixel`, `smooth`, `cinema`, or `ntsc`. |
| `--subsystems` | unsigned decimal `uint32_t` | Explicit subsystem mask applied after persisted settings; range `0..4294967295`. |
| `--mute-buses` | unsigned decimal `uint32_t` | Explicit audio-bus mute mask; range `0..4294967295`. |
| `--core-option` | `key=value` | Repeatable core option applied during session creation. |
| `--shaders` | none | Explicitly enables shaders. |
| `--no-shaders` | none | Explicitly disables shaders. |
| `--manifest` | path | Correlation path logged and emitted in `ready`; Runtime does not parse the file. |
| `--saves-dir` | path | Save-state root chosen by AYTHER Play. |
| `--load-state` | path | Exact save state to resume; failure starts fresh and preserves the file. |
| `--rom-crc32` | hexadecimal text | ROM identity metadata included in save naming. |
| `--frames` | unsigned decimal `uint64_t` | Development/CI frame limit; `0` means unlimited. |
| `--capture-at` | comma-separated positive `uint64_t` values | Non-empty list of logical frame numbers; every element must be greater than zero. |
| `--crash-test` | none | Emits `crash-test` and aborts; test-only launcher isolation hook. |
| `--probe-core` | path | Probes a core and exits without loading a ROM or initializing SDL. |
| `--play-protocol-version` | unsigned decimal `uint32_t` | Negotiates protocol v1 before SDL, Vulkan, core, ROM, or session startup. |
| `--hd-compose` | none | Deprecated compatibility option; accepted but no longer enables the removed path. |

Numeric values are parsed with complete-input and range validation. Empty or
missing values, signs, overflow, trailing text, zero capture frames, and
leading, trailing, or repeated commas are rejected before SDL initialization.
`--core-option` likewise requires `key=value` with a non-empty key.

## Status framing

Machine-readable events are emitted as one unbuffered stdout line:

```text
AYTHER_STATUS <JSON object>\n
```

`StatusEmitter` is the only component allowed to frame these records. It builds
the complete line in memory, encodes every event through one JSON serializer,
writes it with one `fwrite` call, and immediately flushes stdout. Quotes,
backslashes, JSON control characters, and line breaks are escaped; valid UTF-8
is preserved.

Every JSON object includes `"protocol_version":1` before the event-specific
fields. Consumers MUST:

- parse only complete lines beginning with the exact `AYTHER_STATUS ` prefix;
- treat all other stdout and stderr as human diagnostic output;
- reject an unsupported protocol version before starting a session;
- tolerate unknown event names and additional JSON fields within v1; and
- treat process termination without an `exit` event as abnormal.

Play SHOULD pass `--play-protocol-version 1`. Runtime accepts version 1 and
rejects lower or higher explicit versions with reason `protocol.incompatible`
and exit code `65` before subsystem startup. Omitting negotiation is retained
only for older launchers, which must inspect the first status record.

## Events

### `probe`

Successful probe:

```json
{"protocol_version":1,"event":"probe","ok":true,"api":1,"library_name":"...","library_version":"...","valid_extensions":"...","need_fullpath":false,"block_extract":false}
```

Failure reasons are stable machine identifiers: `core.load_failed` for a
library-load failure and `core.invalid` when required Libretro symbols are
missing. Optional `message` text is human-readable and MUST NOT drive logic.

Engine owns the temporary dynamic-library handle and copies the Libretro
metadata. Runtime transfers those owned values into `ProbeSucceededStatus`; the
same Runtime serializer used by every other event performs JSON encoding and
protocol framing.

### `ready`

```json
{"protocol_version":1,"event":"ready","game_id":"...","has_pack":false,"manifest":"..."}
```

Indicates that the session exists. It does not guarantee that Vulkan
post-processing or every optional subsystem initialized successfully.

### `now-playing`

```json
{"protocol_version":1,"event":"now-playing","game_id":"...","title":"..."}
```

Indicates transition into active gameplay. The current implementation uses the
game identifier as the title fallback.

### `warning`

```json
{"protocol_version":1,"event":"warning","reason":"persistence.config_invalid","message":"..."}
```

Reports a typed startup or runtime diagnostic, such as a loaded pack with no
active subsystems, a failed resume, or an invalid input map. The `reason` field
is a stable machine token and the optional `message` field is human-readable.
An event named `warning` can precede a fatal startup exit; callers must use the
process exit code rather than infer recoverability from the event name.
The complete reason taxonomy and recovery classification is normative in
[`runtime-play-protocol.md`](runtime-play-protocol.md).

### `crash-test`

Emitted immediately before the deliberate `--crash-test` abort. It is a test
hook and MUST NOT be interpreted as a clean exit.

### `exit`

```json
{"protocol_version":1,"event":"exit"}
```

or, after a save state is written successfully:

```json
{"protocol_version":1,"event":"exit","savestate":"C:\\path\\to\\state.bin"}
```

Only the `savestate` path from a clean `exit` is eligible for launcher-side
upload or synchronization.

## Process exit codes

| Code | Meaning |
| ---: | --- |
| `0` | Core loaded and required Libretro information symbols were read. |
| `2` | Dynamic library could not be loaded. |
| `3` | Required Libretro symbols were not found. |
| `64` | Invalid command line: a required session argument or option value is missing, malformed, out of range, or outside its documented domain. |
| `65` | Explicit Runtime–Play protocol versions are incompatible. |
| `69` | A required service is unavailable. |
| `74` | A non-recoverable I/O operation failed. |
| `78` | The file supplied through `--input-map` is unreadable or violates the input-map contract. |

Codes and their identifiers are stable protocol-v1 values. CLI and negotiation
errors occur before SDL initialization and include both a machine reason in the
status stream and a human-readable diagnostic. Code `78` follows the same
pre-SDL behavior and emits stable reason `input.map_invalid`.

## Security and trust

A Libretro core is native code loaded into the Runtime process. Probing reduces
launcher exposure but does not sandbox the core. Callers should treat cores,
ROMs, packs, patches, manifests, paths, and save states as untrusted input and
apply distribution-appropriate validation and provenance controls.
