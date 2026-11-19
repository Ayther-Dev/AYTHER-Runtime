# File reservations for Play CE

Status: implemented in `0.1.0-beta.6` (`src/file_reservation.h`). The wire
behavior is summarized in the [Runtime–Play protocol](../runtime-play-protocol.md);
this document is the full contract.

## Requirements

- **RC-1.** When Play sends the revision of a ROM or a pack, Runtime reserves
  the session's files and checks the requested revision before it confirms the
  handover. A different revision or an inaccessible file prevents the
  confirmation and the game start.
- **RC-2.** While the session holds the reservation on Windows, writing,
  deleting and renaming the files is prevented. Runtime releases the files when
  the session ends and also when preparation fails. A platform that does not
  support reservations rejects an explicit request instead of simulating
  success.
- **RC-3.** When no revision is requested, Runtime keeps the previous launch
  behavior, including validation order and diagnostics. The options and their
  confirmation are optional and do not change the existing status protocol.
- **RC-4.** A compatible consumer release documents its version, Engine
  package, artifact hash and launch contract, validated with Play CE.

## Contract

| Item | Value |
| --- | --- |
| Options | `--rom-revision <token>`, `--pack-revision <token>` |
| Token | Shared with Play: volume/file/creation identity, size, modification time in nanoseconds and USN, joined with Play's existing delimiters. |
| Confirmation | `AYTHER_RESERVATION 1` on stdout, only after every requested reservation is held and its token matches. |
| Rejection | Reason `runtime.reservation_failed`, exit 74 (I/O error). A ROM reservation failure is never attributed to the pack. |
| Diagnostics | Technical errors are in English through the existing preparation diagnostic; Play keeps the localized presentation. |

The token detects revision changes. It is not a cryptographic hash of the
content and does not prove where a pack came from.

## Acceptance

- A native test of reservation, release and rejection (`file_reservation`,
  `file_reservation_cli`).
- A real run with tokens computed by Play CE.
- The Runtime suite, which protects the launch without the new options.
