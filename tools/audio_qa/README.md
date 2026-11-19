# `ayther_audio_qa` — replay QA checker

`ayther_audio_qa` replays recorded takes (`.ayr`/`.arp`) or pad timelines
through a QA build of Runtime and keeps reopenable technical evidence: facts,
PCM, checkpoints and results. It attests execution and integrity only; it does
**not** judge whether music restarted or overlapped.

- Evidence layout and compatibility: [docs/audio-qa-evidence.md](../../docs/audio-qa-evidence.md)
- Fact record encoding: [docs/audio-qa-fact-format.md](../../docs/audio-qa-fact-format.md)
- Graphical front end: [tools/replay_qa_launcher](../replay_qa_launcher/README.md)
- Replay window keys: [docs/input-map.md](../../docs/input-map.md#replay-qa-window)

## Build and install

The checker and Runtime must come from the same QA build, against the QA Engine
lock `dependencies/ayther-engine.qa.lock.json`. From the Runtime root:

```powershell
$env:AYTHER_ENGINE_PREFIX = & ./tools/bootstrap_ayther_engine_qa.ps1
cmake --preset windows-qa
cmake --build --preset windows-qa --target ayther_runtime ayther_audio_qa
cmake --install out/build/windows-qa --config RelWithDebInfo --prefix <new-prefix>
```

`windows-qa` sets `AYTHER_REQUIRE_AUDIO_QA_ENGINE_PACKAGE=ON`, which compiles
Runtime's QA session (`--qa-session`, `--probe-pack`). The install creates
`bin/ayther_runtime.exe` and `bin/ayther_audio_qa.exe` (component `qa`) with
their DLLs, shaders, fonts, license and metadata. Use a new prefix; never
overwrite an installation used as a reference. See also
[docs/development.md](../../docs/development.md) («Replay QA»).

## Commands

| Command | Purpose |
| --- | --- |
| `check` | Validate a request, run each take or timeline in Runtime and store evidence. |
| `query --index <query-index.toml> --run-id <id> --producer-id <id> --sequence <n> [--language es\|en]` | Look up a stored relation. |
| `audit --run-directory <dir> --run-id <id> --expected-assignments <n> [--language es\|en]` | Reopen and audit a campaign's evidence. |
| `options --format toml` | Print the option inventory of this build. |

Minimal `check`:

```powershell
& <prefix>\bin\ayther_audio_qa.exe check `
  --runtime <prefix>\bin\ayther_runtime.exe `
  --rom <game.md> --core <core.dll> `
  --pack <game.ay> --trust-registry <trust.toml> `
  --take <take.ayr> `
  --output <evidence-root> --request-id <unique-id> --language es
```

## `check` options

| Option | Value | Default | Rule |
| --- | --- | --- | --- |
| `--runtime` | path | required | Runtime executable. Must offer the four QA contracts 1.0 and the ten mandatory capabilities. |
| `--rom` | path | required | Game image. |
| `--take` | path, repeatable | — | Recorded take. Order is kept; repeats are separate positions. |
| `--timeline` | path, repeatable | — | Pad timeline; runs after the takes. Requires `timeline_replay_v1`. |
| `--output` | path | required | Evidence root (ledger plus one exclusive directory per run). |
| `--core` | path | resolved | Libretro core. |
| `--pack` | path | none | Pack; without it the request runs «Sin pack» and Runtime receives no `--pack`. |
| `--pack-mode` | `hd` \| `original` | `hd` | `original` is equivalent to not loading the pack. |
| `--trust-registry` | path | none | May be omitted only when the effective policy opens the pack without a registry. |
| `--reference` | path | none | Supplies core and conditions not given explicitly. Must describe the same ROM. |
| `--play-manifest` | path | none | Play CE `launch.toml`; same role as `--reference`. |
| `--request-id` | text | generated | 1–256 printable bytes. Reuse it only for a resend; a deliberate repeat needs a new id. |
| `--language` | `es` \| `en` | `es` | Message language of the checker and the replay window. |
| `--presentation` | `none` \| `visible` | `none` | Headless run or visible replay window. |
| `--at-end` | `inspect` \| `close` | `inspect` | Visible window at the natural end: stay paused for inspection, or close once the result is sent. `close` requires `close_at_end_v1`. No effect without presentation. |
| `--profile` | id | — | Applies only with a loaded pack. |
| `--subsystems` | unsigned integer | — | Condition. |
| `--mute-buses` | unsigned integer | — | Condition. |
| `--video-output` | id | — | Condition (Runtime `--output`). |
| `--patch` | path | — | Condition. |
| `--shaders` | `on` \| `off` | — | Condition. |
| `--core-option` | `key=value`, repeatable | — | Condition. |

### Request rules

- At least one `--take` or `--timeline` is required; otherwise
  `missing_required_material: --take`.
- Run names: position 1 is `<run>`; further takes are `<run>-take-N`; a timeline
  at position N (1-based) is `<run>-timeline-N`.
- Core resolution: `--core`, then the Play manifest, then the reference, then
  the Play CE configuration (`cores[<platform>]`, then `default_core` in
  `%APPDATA%\Ayther\play_config.toml`, read only).
- Condition precedence: explicit option, then Play manifest, then reference.
- `--profile` without a pack (or with `--pack-mode original`) is accepted with
  the same conditions: the summary shows `profile=<requested>` and
  `profile_effective=none source=generated`, and Runtime receives no
  `--profile`. With a pack, a profile the pack does not offer is rejected with
  `profile_not_in_pack: --profile`.
- Each take is fully validated before admission, including decompression of its
  initial state (`take_initial_state_invalid: --take[i]`). A v9 take carries the
  FM register shadow at its end; a damaged tail is rejected with
  `take_fm_registers_invalid: --take[i]`. v2–v8 takes have none and are
  accepted. A v10 take (embedded `take_id`) requires `take_format_v10`
  (`runtime_missing_take_format_v10`).
- `--request-id` longer than 256 bytes: `request_id_too_long: --request-id`;
  with control characters: `request_id_invalid: --request-id`.
- A missing trust registry is rejected before admission
  (`material_not_found: --trust-registry`); an unusable pack is rejected with
  the reason from `--probe-pack`. A valid pack without an audio catalog is not
  an error: the take plays and records `assignments=0`.
- Pasted paths wrapped in double quotes are used without the quotes.
- Evidence files are written and reopened through the extended path form
  (`\\?\`), so a long output root does not exceed `MAX_PATH`.

### Pack probe

Before admission the checker runs `ayther_runtime --probe-pack`. The registry is
checked first with the same rules as a launch: a missing, unreadable or invalid
registry (including a quoted path) exits 78 with `trust_registry_invalid`,
which the checker reports on `--trust-registry`. `pack_untrusted` is reserved
for a pack Engine refuses for its signature or trust policy (unknown or revoked
key, validity, scope); any other open failure is `pack_open_failed`. The full
reason list is in [`--probe-pack`](../../docs/process-protocol.md#probes).

## Pad timelines

A pad timeline is a text file that drives the pad instead of a take. One
instruction per line; `#` starts a comment at the start of a line or after
whitespace; keywords and buttons are case-insensitive.

```text
start power_on              # default if absent; must be the first instruction
start state ../bookmarks/audio-test.sav   # a Lab bookmark (.sav), relative to the timeline
wait 5s                     # frames pass with the held buttons
press start                 # pressed for 4 frames, then released
press a 100ms               # explicit duration
hold right 0.1s             # like press, duration required
down b                      # add to held buttons; no time passes
wait 12f
up b                        # remove from held buttons
press a+c 6f                # combination
```

- Genesis buttons: `a b c x y z start mode up down left right`, joined with
  `+`. They map to the RetroPad as Genesis Plus GX does (A→Y, B→B, C→A, X→L,
  Y→X, Z→R, Mode→Select).
- Durations: `<decimal>s`, `<decimal>ms` or `<integer>f`. Seconds become frames
  in Runtime at the core's timing (`llround(seconds × fps)`): `0.1s` is 6 frames
  at 59.92 fps and 5 at 49.70. Only `f` is timing-independent. A step that
  rounds to zero frames is an error.
- Each frame's mask is the held buttons plus the current `press`. Two `press`
  steps without a `wait` are one continuous press.
- Limits: 64 KiB, 4 096 instructions, 1–54 000 compiled frames (900 s at the
  core's timing).
- `power_on` starts the ROM in the new session, so the audio event detector sees
  every FM write from frame 0. `state <path>` restores a Lab bookmark
  (`<project>/bookmarks/<name>.sav`, raw serialized state) or a Runtime state
  (`.bin` with an `AYTHSTAT` header) before the HD boundary.
- A bookmark has no FM register shadow; the Lab writes it beside it as
  `<name>.fm` (512 bytes). If present it is pinned as a material and seeded
  after the HD boundary, like a v9 take (`fm_seeded = true`). Without it the
  evidence says `fm_sidecar = "absent"`, the report warns
  `timeline_fm_seed_absent`, and a timeline starting with music playing has the
  same risk as a take started mid-music.
- Evidence: the same as a take plus a `timeline_compiled` fact (producer
  `inspection`) with `fps`, `frames`, `steps`, `start` (`power_on` or `state`),
  `state_id`, `fm_sidecar` (`present`, `absent`, `not_applicable`) and
  `fm_seeded`. The `audio_qa_replay` line says `material=timeline`; the request
  summary row says `kind = 'timeline'`.

Preflight rejections (field `--timeline[j]`):

| Code | Cause |
| --- | --- |
| `timeline_syntax_invalid` | Syntax error; also prints `audio_qa_diagnostic: timeline_syntax_invalid field=--timeline[j] line=<n> reason=<code>`. |
| `timeline_too_long` | More than 54 000 frames or 900 s at the core's timing. |
| `timeline_empty` | No frames. |
| `timeline_state_unavailable` | The bookmark cannot be read. |
| `timeline_fm_sidecar_invalid` | The `.fm` beside the bookmark is not 512 bytes. |

Timeline, bookmark and `.fm` are pinned by SHA-256 like any material and are
part of the conditions identity. The same grammar, limited to
`start power_on`, drives Runtime's `--input-script`.

## Output

Before admission every effective value is printed with its origin:

```text
audio_qa_effective: <key>=<value> source=explicit|play_manifest|reference|environment|default|generated
```

`explicit` means the option was written, even if its value equals the default;
`default` means it was not written.

| Prefix | Content |
| --- | --- |
| `audio_qa_status` | Admission and technical states. |
| `audio_qa_replay` | Take identity, positions, relations and preservation. |
| `audio_qa_summary` | Aggregate result and its code. |
| `audio_qa_error` | Stable technical code. |
| `audio_qa_message[es\|en]` | Localized text; never parse as a code. |
| `audio_qa_diagnostic` | Detail of a preflight rejection. |

Exit codes (aggregate priority 4, 3, 2, 0):

| Code | Meaning |
| ---: | --- |
| 0 | Every take complete. |
| 2 | Result or evidence incomplete. |
| 3 | Invalid invocation or request. |
| 4 | Minimal evidence could not be kept. |

`options --format toml` uses the schema of
`tests/audio_qa/fixtures/rf1-option-inventory-beta8.toml`: `ref` is the Runtime
version and `commit` the source commit of the build (checkout `HEAD` at build
time, or `AYTHER_RUNTIME_SOURCE_COMMIT` when set).

## Tests

Public end-to-end replay (synthetic core, generated take, no private material).
It expects exit 0 with technical result `complete`, audible fields left
unevaluated, a private Runtime data root and an unchanged user data tree:

```powershell
cmake --build --preset windows-qa --target ayther_runtime ayther_audio_qa `
  audio_qa_public_recording_fixture synthetic_libretro_core
ctest --preset windows-qa -R "^audio_qa_(public_recording_fixture|real_replay_integration)$" --output-on-failure
```

All checker tests: `ctest --preset windows-qa -L audio_qa -LE gpu`. Visible
replay tests need `windows-qa-gpu` and a desktop session.

| Test or tool | Purpose |
| --- | --- |
| `audio_qa_fact_throughput` | B06: sustained load then burst, up to 10 000 000 facts. |
| `audio_qa_channel_disconnect_latency` | B10: 100 Windows channel breaks, each noticed within 10 ms. |
| `audio_qa_legacy_runtime_cli` | Optional; given a historical Runtime binary via CMake, checks that no `.aqf`/`.aqp` appears. |
| `audio_qa_private_frame_instrumentation_cost` | B13; built but not registered in CTest (needs private material). See below. |

B13 measures producer work inside `set_input` and `step`, including delivery of
every fact to an exact counter, separate from pacing (copies, queues and
persistence are covered by B05/B06). It takes five positional paths, pins the
measuring thread, warms up symmetrically and runs three pairs with three
alternating repetitions per condition, using calibrated effective thread cycles
on Windows. Each pair requires P95 on ≤ 1.05 × P95 off, P99 difference ≤ 1 ms,
equal fact counts and identical state and decisions. Run once per selected
take:

```powershell
& '<build>/tests/audio_qa/RelWithDebInfo/audio_qa_private_frame_instrumentation_cost.exe' `
  '<core>' '<rom>' '<pack>' '<trust-registry>' '<take>'
```

## Normative reference

Limits and rules the implementation enforces. Task ids (QA-*nnn*) name the
originating requirement in the audio-QA spec.

### Protocol and transport

| Area | Rule |
| --- | --- |
| QA header (QA-090) | 40 fixed bytes, little-endian; validates magic, exact version, size, type, flags, length, direction/channel, sequence and reserved field before exposing metadata. Body 1–262 104 bytes; message at most 256 KiB. |
| Control (QA-091) | Request in TOML on type `request`/channel 1; accepted or rejected answer as `session_status`/channel 2. Identity fields and the ordered take list survive the round trip. |
| `fact_batch` (QA-092) | 1–1024 records, each with a u32 LE length prefix, at most 64 KiB including the prefix. See [fact format](../../docs/audio-qa-fact-format.md). |
| `audio_chunk` (QA-036, QA-093) | Two u32 LE lengths delimit TOML metadata and interleaved PCM. S16LE/S24LE/S32LE/F32LE, up to 192 kHz, 8 channels, 256 discontinuities, 256 KiB payload. SHA-256 of the PCM bytes is required and verified; frames × channels × width must match. |
| Channels (QA-094, QA-120) | Inherited pipe pair, never stdout/stderr; the child is created with an explicit handle list. Facts, PCM and logs are drained by independent readers with independent limits; stderr pressure never stalls QA data. |
| Capabilities (QA-039, QA-119, QA-173) | Before any channel or material is passed, the checker runs only `--qa-capabilities`, captures at most 64 KiB within 2 s, and requires the four contracts at 1.0, the ten mandatory capabilities and their limits (at most 64 capabilities and 16 versions per contract). Otherwise `runtime_incompatible`, no game start. |
| Process launch (QA-118) | The Runtime executable is identified and revalidated before launch; arguments are encoded individually, the environment comes from an allow list, no shell. |

### Takes and replay

| Area | Rule |
| --- | --- |
| Take header (QA-096) | Recognized by content (`ARP1`), not by extension; Engine versions 2–10. Distinct codes for empty input, truncated header, bad magic, unsupported version. |
| Take layout (QA-097) | Take and decompressed initial state up to 64 MiB; 1–54 000 frames. Range arithmetic checks strings, compressed state and inputs before any allocation. |
| Input source (QA-098, QA-103) | RetroPad u16 LE masks from frame 0 to N−1, never repeated; trims and statistics are ignored. Each mask goes once through `set_input(0, buttons)` and each confirmed input produces exactly one `step()`; no `replay_seek`, nothing after N. |
| Restore (QA-099, QA-100) | The initial state is decompressed and the source validated before Engine is mutated; inputs are exposed only after a successful `unserialize`. A failed preparation runs no frame. |
| HD origin (QA-037, QA-101, QA-102) | Absent HD state uses a verified `fresh` start. A supplied state needs identity, frame and the three payloads; a rejection keeps code and section and runs `fresh`. The snapshot is published once before the next recorded input is read. Caps: 256 voices/queues, 4096 windows/requests, 64 MiB per image. |
| External controls (QA-104) | Keyboard game input, rewind and fast-forward are dropped without touching the cursor; cancellation stays available. |
| Phases (QA-108, QA-109) | `preparing` → `ready` → `playing` → `closing` → `closed`; no skips. The playback terminal is separate from the evidence result. |
| Progress and close (QA-106, QA-107) | The end is emitted only after N inputs and N frames. Close freezes production, finishes voices and drains only pre-boundary staging. |
| Cancellation (QA-114, QA-121) | Applied at a frame boundary; keeps request and apply positions, last frame and terminal `cancelled`. Delivered over the control channel as request, ack, application and confirmed cessation (only after the child's exit is observed). |
| Terminals (QA-115) | The first effective terminal wins; late notifications never rewrite it. |
| Requests (QA-111–QA-113, QA-127) | Identical resend returns the known state; same id with different content is a conflict; a new id is an independent repeat. Each take runs in a new process, in order. |

### Watchdogs

| Phase | Limit |
| --- | --- |
| Preparation (QA-122) | 120 s without a new stage; 300 s total. Heartbeats and repeated stages do not extend it. |
| Playback (QA-123) | 5 s without a higher completed frame. |
| Close (QA-124) | 10 s without durable progress; 30 s total. Only more durable bytes reset it. |
| Cancellation (QA-125) | Answer within 2 s; wait ends 5 s after the request. Timeout is never treated as cessation. |

### Evidence storage

| Area | Rule |
| --- | --- |
| Run directory (QA-136) | Exclusive per `run_id`; a collision keeps the old content and yields `evidence_directory_collision`. Ids with separators or `..` are rejected. |
| `reference.toml` (QA-137) | Version 1.0, created exclusively; never truncated or replaced. |
| Fragments (QA-138, QA-139) | `fragments/facts-<seq>.aqf` (`AYTFACT1`) and `audio/pcm-<seq>.aqp` (`AYTPCM01`); existing paths are never replaced. |
| Durable publish (QA-140, QA-148, QA-149) | Exclusive temp file in the same directory, `FlushFileBuffers`, `MoveFileExW` with `MOVEFILE_WRITE_THROUGH`; failure keeps the previous target. A write budget exhausted mid-file yields `storage_exhausted`; the checkpoint reports `publish_failed`. |
| Checkpoint (QA-141) | `checkpoints/current.toml` 1.0, increasing sequence; lists only durably published fragments, revalidated before listing. |
| Ledger (QA-143) | `request-ledger.toml` 1.0: up to 1024 requests, increasing generation, 64 MiB. |
| Recovery (QA-144–QA-146) | A run that did not reach `closed` is recovered as `incomplete` without resuming; audits are read-only and report `artifact_invalid` or `artifact_changed`. Unknown major/minor or undeclared fields are rejected; originals are never migrated. |
| Live queues | Producers 3 and 5: 4096 facts; producer 4: 1024; producer 6: 384; 2048 PCM blocks of 1024 bytes. Aggregate 65 202 432 bytes, below the B05 limit of 64 MiB (67 108 864 bytes). |
| Memory (QA-188, B05) | Increment at most 256 MiB for the checker and 128 MiB for Runtime/Engine, measured in isolated child processes. |

### Audit and queries

| Area | Rule |
| --- | --- |
| Fact sequences (QA-150, QA-151) | Identical retransmission (same `fact_id` and content) is idempotent; a gap or conflicting reuse marks the trace `incomplete`. Internal `cause_ids` are resolved after the whole trace is reopened. |
| Shared state (QA-152) | Facts with `shared_state_order=not_applicable` stay independent; known orders must be continuous per run. |
| PCM (QA-153) | Sequence, capture identity, format, timeline and sample continuity are checked; no audio is generated. |
| Audio relations (QA-171, QA-172) | The trace from `detector_input_batch` through candidate, selection, request, decision/effect and `hd_mix_participant` must share one occurrence; otherwise `audio_trace_incomplete`. `relationships_reopened=true` only after re-walking reopened facts. |
| Coverage (QA-154–QA-157) | Inventory and stages (candidacy, selection, start, progress, end) are independent; keyed by `pack_content_id + ordinal`; at most 4096 observations per call. A partial zero stays unknown. |
| Trace query (QA-158) | Explicit paths use only `cause_ids`; temporal correlation needs a supplied link with a reason. At most 4096 facts, segments and links. |
| Domain limits (QA-033–QA-035, model) | 4096 materials, 256 conditions, 256 differences, 4096 bytes per value, 256-byte identities; up to 256 causes and 256 shared orders per fact; 1–1024 takes per request; TOML metadata at most 256 KiB; `uint64` values as decimal strings. |

### Library components not used by `check`

These modules exist with unit tests but `check` does not call them in this
version: pinned take, pack and game-material images (QA-130–QA-132), the
material consumption guard (QA-135), the WAV view (QA-147), the coexistence
query (QA-159), the condition lock (QA-105), the session reservation (QA-110),
the checkpoint threshold (QA-142: one publication at the first of 1 s or
1 MiB pending), the evidence quota (QA-186: 10 GiB free,
8 GiB per run) and the staging budget (QA-187: 8 GiB plus 2 GiB margin). The
material protection `check` applies is described in
[isolation](../../docs/audio-qa-evidence.md#isolation-and-private-materials).
