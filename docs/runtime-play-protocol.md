# Runtime–Play protocol v1

Status: **stable**. Last reviewed: 2026-11-19.

Protocol v1 is the wire contract between AYTHER Runtime and the launcher that
spawns it (AYTHER Play CE). It is the only stable promise Runtime makes: the
Runtime product itself is a prerelease (see [status](status.md)), and its
package layout, Engine ABI, configuration files and save-state format are not
covered by this guarantee.

The protocol covers status framing, version negotiation, process exit codes and
reason identifiers. Event payloads and the command line are described in
[`process-protocol.md`](process-protocol.md).

## Status framing

Runtime writes each protocol record as one unbuffered stdout line:

```text
AYTHER_STATUS <JSON object>\n
```

- `StatusEmitter` is the only component that frames records. It builds the
  complete line in memory, encodes it with one JSON serializer, writes it with
  one `fwrite` call and flushes stdout.
- Quotes, backslashes, JSON control characters and line breaks are escaped;
  valid UTF-8 is preserved.
- Every object starts with `"protocol_version":1` and `"event"`, followed by
  the event fields of the typed values in `src/status_emitter.h`. The `probe`
  event is the exception: it carries the Engine probe contract version
  (`2`); see [`probe`](process-protocol.md#probe).

Consumers MUST:

- parse only complete lines that begin with the exact `AYTHER_STATUS ` prefix;
- treat all other stdout and stderr as human diagnostics;
- reject an unsupported protocol version before starting a session;
- ignore unknown events and unknown fields;
- branch on `reason`, never on the optional, localizable `message`; and
- treat process termination without an `exit` event as abnormal.

Within v1, fields may be added; existing fields do not change type or meaning.

## Negotiation

Play SHOULD pass `--play-protocol-version N`. Runtime checks it before SDL,
Vulkan, core, ROM or session startup.

| Value of `N` | Runtime behavior |
| --- | --- |
| `1` | Normal startup. |
| any other value | Emits `warning` with `reason: "protocol.incompatible"` and exits `65`. |

A launcher that omits the option still works, but must inspect the first
`AYTHER_STATUS` record and stop if its version is unsupported.

## Optional file reservations

Play may pass `--rom-revision` and `--pack-revision` with the tokens of files it
holds reserved. On Windows, Runtime acquires its own reservations, checks the
revisions and prints `AYTHER_RESERVATION 1` before continuing; Play can then
release its handles. The confirmation line is not an `AYTHER_STATUS` record and
does not change protocol v1.

- A rejected reservation emits `runtime.reservation_failed`, exits `74` and
  prints no confirmation.
- Without these options, startup is unchanged and no confirmation is printed.
- Platforms without reservation support reject any explicit request.

Token format and criteria: [reservation contract](specs/runtime-reservations.md).

## Stable exit codes

| Code | Identifier | Meaning |
| ---: | --- | --- |
| 0 | `success` | Successful command or session. |
| 1 | `startup_failed` | General startup failure (SDL, session creation). |
| 2 | `core_load_failed` | Core or native provider could not be loaded. |
| 3 | `invalid_core` | File loaded but is not a compatible core, or the native provider does not support the ROM's system. |
| 4 | `pack_open_failed` | An explicitly requested pack could not be opened, validated, activated or reloaded. |
| 64 | `cli_usage` | Invalid command line. |
| 65 | `protocol_incompatible` | Incompatible protocol versions, or a save state of another emulator generation. |
| 69 | `service_unavailable` | Required service unavailable. |
| 74 | `io_failure` | Non-recoverable I/O failure, including a rejected file reservation. |
| 78 | `configuration_invalid` | Invalid `--input-map` or `--trust-registry` file. |

Probe modes have their own codes; see [`probe`](process-protocol.md#probe) and
[`--probe-pack`](process-protocol.md#probes).

## Error taxonomy

| Domain | Reason | Severity and recovery |
| --- | --- | --- |
| CLI | `cli.invalid_argument` | Fatal; fix the invocation. Exit 64. |
| CLI | `args.provider_conflict` | Fatal; `--provider tessera` was given a `--core`. Exit 64. |
| Core | `core.load_failed` | Fatal for the launch. |
| Core | `core.invalid` | Fatal for the launch. |
| Core | `core.unsupported_system` | Fatal; the native provider does not emulate the ROM's system. Exit 3. |
| Pack | `pack.rejected` | Recoverable only for a pack discovered by convention (no explicit `--pack`); the session continues without it. |
| Pack | `pack.open_failed` | Fatal; requested pack unavailable, incompatible or rejected by Engine trust policy. Exit 4. |
| Pack | `pack.trust_registry_invalid` | Fatal; the registry is missing, unreadable or malformed. Exit 78. |
| Pack | `pack.no_active_subsystems` | Warning; the session continues as original. |
| State | `state.restore_failed` | Recoverable; the session starts from power-on and the file is kept. |
| State | `state.generation_mismatch` | Fatal before the first frame; the state was written by another emulator generation. Exit 65. Play may offer to start without the state. |
| State | `state.save_failed` | Recoverable; the save was not published. |
| Vulkan | `vulkan.unavailable` | Recoverable; headless or original path. |
| Vulkan | `vulkan.initialization_failed`, `vulkan.frame_failed` | Fatal for GPU presentation. |
| Vulkan | `vulkan.postprocess_degraded` | Warning; plain presentation. |
| Persistence | `persistence.config_invalid`, `persistence.io_failed`, `persistence.capture_failed` | Recoverable; the prior valid data is kept. |
| Protocol | `protocol.incompatible` | Fatal before session startup. Exit 65. |
| Protocol | `runtime.reservation_failed` | Fatal; a requested file reservation failed. Exit 74. |
| Input | `input.map_invalid` | Fatal before SDL startup. Exit 78. |

Save-state generation: a v2 save-state header records `emulator_id` and
`determinism_contract`. Files without the header, or in v1, count as
`gpgx` / `1`.

The normative definitions are `RuntimeErrorCode`, `error_reason`,
`error_severity` and `RuntimeExitCode` in `src/runtime_error.h`; tests pin
their values.

## Conformance suite

`tests/protocol_v1_conformance.cmake` is the portable consumer oracle. Runtime
validates its real `StatusEmitter` output with it; Play can run the same script
against captured JSONL without linking Runtime:

```console
cmake -DINPUT_FILE=<capture.jsonl> -DEXPECT_VALID=ON \
      -P tests/protocol_v1_conformance.cmake
```

The fixtures cover every current event, all required field types, the stable
reason vocabulary, older, current and future negotiation, unknown fields and
events, UTF-8 and escaped characters, and a truncated partial write.
