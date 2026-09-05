# Runtime–Play protocol v1

Status: protocol v1 is stable. Last reviewed: 2026-09-03.

This wire contract is stable independently of the product release channel.
AYTHER Runtime itself remains a prerelease at `0.1.0-beta.2`; its package, ABI, save-state,
and general product surfaces are not covered by the protocol-v1 guarantee.

Runtime writes one record per line to stdout. A protocol record starts with
`AYTHER_STATUS ` and is followed by one UTF-8 JSON object. Diagnostic log lines
without this prefix are not protocol records.

Every object contains:

- `protocol_version`: unsigned protocol version; currently `1`.
- `event`: stable event identifier.
- Event fields documented by the corresponding typed value in
  `src/status_emitter.h`.

Consumers must ignore unknown object fields and unknown events. Fields may be
added in a compatible v1 release, but existing fields do not change type or
meaning. Runtime emits machine-readable `reason` values separately from the
optional, localizable human `message`.

## Negotiation

Play should pass `--play-protocol-version N` before starting a session.

| Play version relative to Runtime | Runtime behavior |
| --- | --- |
| `N == 1` | Normal startup. |
| `N < 1` | Runtime emits `reason: "protocol.incompatible"` and exits with code `65` before startup. |
| `N > 1` | Runtime emits `reason: "protocol.incompatible"` and exits with code `65` before SDL, Vulkan, core, ROM, or session startup. |

Omitting the option retains compatibility with launchers predating explicit
negotiation. Such a launcher must still inspect the first `AYTHER_STATUS`
record and stop if its version is unsupported. Passing a numeric version is an
explicit contract and only the documented current version is accepted.

## Stable exit codes

| Code | Identifier | Meaning |
| ---: | --- | --- |
| 0 | `success` | Successful command/session. |
| 1 | `startup_failed` | General startup failure. |
| 2 | `core_load_failed` | Core could not be loaded. |
| 3 | `invalid_core` | File loaded but is not a compatible core. |
| 64 | `cli_usage` | Invalid command-line contract. |
| 65 | `protocol_incompatible` | Play and Runtime declared incompatible protocol versions. |
| 69 | `service_unavailable` | Required service unavailable. |
| 74 | `io_failure` | Non-recoverable I/O failure. |

## Error taxonomy

`reason` is a stable machine key. `message` is explanatory text and must never
be used for branching.

| Domain | Stable reasons | Severity / recovery |
| --- | --- | --- |
| CLI | `cli.invalid_argument` | Fatal; fix invocation. |
| Core | `core.load_failed`, `core.invalid` | Fatal for the requested launch. |
| Pack | `pack.rejected` | Recoverable; continue original. |
| Pack | `pack.no_active_subsystems` | Warning; continue original. |
| State | `state.restore_failed` | Recoverable; start from the beginning. |
| State | `state.save_failed` | Recoverable for the session; save was not published. |
| Vulkan | `vulkan.unavailable` | Recoverable; headless/original path. |
| Vulkan | `vulkan.initialization_failed`, `vulkan.frame_failed` | Fatal for GPU presentation. |
| Vulkan | `vulkan.postprocess_degraded` | Warning; use plain presentation. |
| Persistence | `persistence.config_invalid`, `persistence.io_failed`, `persistence.capture_failed` | Config/capture failures preserve the prior valid data. |
| Protocol | `protocol.incompatible` | Fatal before session startup. |

The normative definitions are `RuntimeErrorCode`, `error_reason`,
`error_severity`, and `RuntimeExitCode` in `src/runtime_error.h`; tests pin their
values.

## Shared conformance suite

`tests/protocol_v1_conformance.cmake` is the portable consumer oracle. Runtime
validates its real `StatusEmitter` output with it; AYTHER Play can run the same
script against captured JSONL without linking Runtime:

```console
cmake -DINPUT_FILE=<capture.jsonl> -DEXPECT_VALID=ON \
      -P tests/protocol_v1_conformance.cmake
```

The checked-in fixtures cover every current event, all required field types,
the stable reason vocabulary, older/current/future negotiation, unknown fields
and events, UTF-8 and escaped characters, and a truncated partial write.
