# Audio QA evidence, recovery and compatibility

`ayther_audio_qa` keeps reopenable technical evidence of a replay. It records
what was observed and how intact the record is; it does **not** decide whether
the music restarted or overlapped. This page is the reference for the evidence
directory, the visible replay window and the compatibility rules. The command
line is in [tools/audio_qa](../tools/audio_qa/README.md); the record encoding
is in [audio-qa-fact-format.md](audio-qa-fact-format.md).

## Visible replay

`ayther_audio_qa check --presentation visible` opens Runtime's normal
presentation with audio on the playback device. Image, mix and records come
from one session fed by the take. `--presentation none` (default) is the
automated run with dummy audio; it does not attest audiovisual observation.

- Requires QA protocol 1.1 with the capabilities `visible_replay_v1` and
  `inspection_v1`.
- Game controls, rewind and profile changes are blocked during the take; no key
  reaches the game.
- Each take opens its own window. Closing it cancels the take and keeps what is
  available.

### Pad timelines

A pad timeline (`--timeline`, grammar in
[tools/audio_qa](../tools/audio_qa/README.md#pad-timelines)) replaces a take:
Runtime starts the ROM from power-on or restores a Lab bookmark, and compiles
the timeline to one RetroPad mask per frame at the core's timing. From then on
the run is that of a take: same per-frame input, same close, same evidence.

- Requires the capability `timeline_replay_v1`. The request sent to Runtime
  carries `material_kind = "timeline"`, a key a take request never writes, so
  an older Runtime still serves takes.
- The evidence adds a `timeline_compiled` fact (producer `inspection`) with
  `fps`, `frames`, `steps`, `start`, `state_id`, `fm_sidecar` and `fm_seeded`.
- In `request-summary.toml` the timeline row says `kind = 'timeline'`; the
  timeline, its bookmark (`timeline_state`) and its `.fm` sidecar
  (`timeline_fm_sidecar`) are listed as materials with their SHA-256.

### Inspection controls

| Key | Effect |
| --- | --- |
| Space | Pauses at the end of the frame in progress, which stays presented; with no frame in progress, on the last one shown. In pause, resumes from the frame after the one shown, without speeding up to recover the paused time. |
| Left / Right | In pause, step one frame back or forward within the take. Navigation is silent and is recovered from checkpoints with the recorded input. |
| I | Shows or hides the frame's debug record. Does not change playback or position. Each take starts with the record hidden. |
| Page Up, Page Down, Home, End, mouse wheel | Scroll the visible debug record. |
| Esc | Requests closing the window, like its close button (the window is full screen, without a title bar). Before the natural end it cancels; after it, it only closes the window and the post-end inspection, and the confirmed result does not change. |

- A held key does not repeat.
- Without focus no key acts. On regaining focus, a key still held stays blocked
  until released.
- Left and Right together move nothing.
- While preparing or recovering, the window shows «No disponible» / «Ocupado»
  (Spanish UI; «Not available» / «Busy» in English) and does not queue
  commands.

**Status line.** Bottom right, always visible: take time in seconds, frame over
total, the pad buttons of that frame with their Genesis names (label «Mando»,
`-` for none) and, for a timeline, the current frame-consuming step as written,
with its remaining seconds (label «Paso»). With `I`, the debug record repeats
«Mando» and «Paso» as rows.

**Event history.** For a timeline, bottom left: each step reached is appended
as a row with its start second and its text (for example `5.00 s  press
start`); new rows enter at the corner and push older ones up. A `down`/`up`
step, which consumes no frames, is appended with the step that follows it. The
list only grows: stepping back removes nothing, and only the last rows that fit
are shown.

**Natural end.** When the last frame of the last take completes, the linear
result is confirmed and sent first; then the window pauses on that frame with
the hint «Fin de la reproducción. Flechas: inspeccionar · Esc: cerrar la
ventana». An intermediate take moves on to the next one, unless a pause was
pending: then it pauses and Space starts the next take. With `--at-end close`
(launcher field «Al terminar») the window closes itself once the result is
sent, as a run without presentation ends; Runtime must offer `close_at_end_v1`
or the request is rejected with `runtime_missing_close_at_end_v1: --runtime`.

**Post-end inspection.** Navigating after the natural end opens a separate
traversal, `post_end_inspection`, on a session rebuilt from checkpoints. It has
its own `run_id` (`<run_id>-inspection-<n>`) and its own terminal, and never
changes the confirmed result.

**Interrupted presentation.** Loss of video, of the audio device in use, or a
minimized window stops the take at the last completed frame. On recovery the
take stays paused with the incident recorded. Cancellation remains available
while interrupted.

### Replay result

`runs/<run_id>/replay-result.toml` atomically keeps the terminal: mode, audio
backend, effective profile, presented frames, count of affected frames and
their enclosing interval (zero-based take indices), initial resolution,
HD/shaders, and the historical physical equivalence as not verified. The
enclosing interval may include unaffected frames; it does not represent a
continuous loss.

Terminal 1.4 adds, separately: `playback` (`natural_end`, `cancelled`,
`failed`, `interrupted`), `traversal` (`linear`, `inspection`,
`post_end_inspection`), `linear_completed`, `evidence_reasons`, `ended_paused`,
`user_pause_ms` and `interruptions`. Terminals 1.2 and 1.3 are still read;
their traversal and playback are reported as unknown, never as linear.

Rules:

- A window, video, audio or cadence failure yields an incomplete result and
  keeps the safe replay and its records.
- Cadence threshold: one core period beyond the frame's due time.
- Exception (DI-25): in continuous playback, isolated late frames are recorded
  as `cadence_degraded` but do not make the observation incomplete if none is
  more than 3 periods late and there is at most one per 1 000 take frames
  (minimum one). The terminal counts them in the optional
  `presentation.tolerated_late_frames` (absent means 0).
- A navigation (`traversal = inspection`) presents in playback only the frames
  it plays; the rest it reaches by moving, so it is not required to present
  every consumed frame. A failure while closing it is still reported as
  `inspection`.
- No complete status implies that the musical failure was evaluated or that
  physical device synchronization was measured.
  `audible_restart_observed` and `audible_overlap_observed` show
  `not_evaluated` when there is no evaluation, even if the window was
  presented. `false` is reserved for an explicit negative evaluation; it is not
  the default.

## Evidence directory

The `--output` root holds `request-ledger.toml` and one exclusive directory per
`run_id`:

```text
<output>/
  request-ledger.toml
  requests/<run_id>/request-summary.toml
  runs/<run_id>/
    replay-result.toml
    traversal.toml
    fragments/facts-00000000000000000001.aqf
    audio/pcm-00000000000000000001.aqp
    checkpoints/current.toml
    query-index.toml
  runs/<run_id>-take-2/…
  runs/<run_id>-inspection-1/…
```

Some files appear only when their phase publishes. An absence is reported as
unknown or incomplete and never turned into a zero count. A run never replaces
another run's directory.

## Traversals, visits and summary

**`traversal.toml`** (schema 1.2, one per traversal; 1.1 is still read, with
audio segments and exclusions reported as unknown) describes how the take was
traversed and is rewritten durably when the result is confirmed:

- the kind: `linear`, `inspection` or `post_end_inspection`;
- the linear segments;
- the frame visits, each with its visit number;
- resumptions and interruptions.

**Traversal kind.** A pause without navigation keeps the traversal linear. The
first Left or Right step turns it into `inspection` for good, and that
traversal is never credited as a complete linear playback.

**`inspection_event`.** Each applied control is a fact of this class, with
`seq` (visit order from 1); the control (`pause`, `resume`, `step_forward`,
`step_back`, `recover_failed`, `overlay_toggle`, `interrupted`, `recovered` or
`advance_take`); the frame before and after; the visit; and the take time.

**`render_frame`.** Each visit to a frame, produced or recovered, yields a fact
of this class with the frame, the visit, whether it could be composed (or why
not), the occurrence count, and the processing time and instantaneous FPS when
measured. A repeated visit has another visit number. A paused frame produces no
new records.

**Silent production.** Facts and PCM produced silently while navigating or
recovering are not evidence: Runtime discards them in its sink and declares the
discarded facts (see fact exclusions below).

**Segmented audio (evidence 1.2).** A take's PCM is written in linear segments:
segment 0 starts with the take and Runtime opens the next one at each resume.
Each `.aqp` block names its segment and never spans two.

- `traversal.toml` 1.2 adds `audio_segments`: per segment, `segment`, the
  frames played in it (`frame_from`, `frame_to`), the timeline and rate, the
  sample interval (`sample_begin`, `sample_end`, decimal) and the blocks that
  form it. Toggling the debug record during playback splits the traversal
  segment but not the audio segment.
- PCM continuity is audited within each segment. In an `inspection` traversal a
  jump between segments is not a loss: stepping forward produces frames
  silently and the timeline advances; stepping back restores a checkpoint and
  the timeline goes back.
- A linear traversal still requires one continuous interval, even if a pause
  splits it into segments.
- A segment with frames and no PCM, or PCM of a segment without frames, makes
  the evidence incomplete (`pcm_segment_mismatch`). With all segments
  contiguous and complete, an inspection's audio is complete but never credited
  as linear audio.

**Fact exclusions (evidence 1.2).** Engine facts produced silently by a
recovery are not evidence, and their producer's sequence has a gap. Each
recovery declares it:

- at the end of the recovery, Runtime emits a `fact_exclusion` fact (producer
  `inspection`) for each affected Engine producer, with `recovery` (order of
  the recovery in the run, from 1), `producer` (`engine-<n>`), `sequence_from`
  and `sequence_to` (excluded interval, both inclusive) and `cause`
  (`silent_recovery`). It is stored with the other facts in the `.aqf`
  fragments;
- `traversal.toml` 1.2 adds `fact_exclusions` with the same fields (sequences
  as decimals), next to `audio_segments`;
- the trace audit treats a declared gap as an exclusion, not a loss. An
  undeclared gap is still a loss, as is a declaration that covers a kept fact,
  excludes the same sequence twice or has another cause. A cause pointing to an
  excluded fact counts as excluded, not unresolved;
- an inspection whose only gaps are declared exclusions therefore has complete
  evidence (`loss_free`), with `traversal = inspection` and no linear credit;
- evidence 1.0 or 1.1, or an earlier 1.2 without `fact_exclusions`, is still
  read; since it declares nothing, its gaps remain losses and the evidence is
  incomplete.

**`request-summary.toml`** (schema 1.2) is the confirmed, durable summary of the
request:

- the selections with their origin, SHA-256 and `conditions_id`;
- each take's result by position, with `playback`, `traversal` and `evidence`
  separately;
- the joint result.

Querying an already confirmed request returns this summary unchanged.

**Complete evidence.** Evidence is `complete` only if all of these were kept and
checked: identification; conditions; every position and control; the result;
and the data of every visited frame, with no known loss.

## Facts and audio

Each `.aqf` starts with `AYTFACT1`, container version 1.0, sequence, record
count, payload size and SHA-256. The payload is a `fact_batch` whose records
use schemas 1.0–1.4 ([format](audio-qa-fact-format.md)); detailed facts are
written as binary 1.4 `AQF4`. Reading validates header, limits, length,
sequence, hash, count and model before returning facts.

Each `.aqp` starts with `AYTPCM01`, container version 1.0, sequence, sizes and
SHA-256. The body holds metadata and interleaved PCM: S16LE, S24LE, S32LE or
F32LE, up to 192 kHz and eight channels within the negotiated limits. The
reader validates capture, format, timeline, ranges, hash and bytes. It never
invents silence or completes songs; a derived WAV leaves the `.aqp` files
intact. Metadata 1.1 adds causes; 1.2 adds `segment`, written only for segments
after 0. A 1.0 or 1.1 block reads as segment 0, so older evidence is still
audited as one interval.

## Durability and recovery

- `checkpoints/current.toml` (schema 1.0, increasing sequence) lists up to 4096
  published artifacts with class, safe relative path, sequence, size and
  SHA-256. The new checkpoint is written and flushed before the atomic
  replacement; if publication fails, the previous one stays valid.
- The ledger follows the same principle and distinguishes new request, resend,
  conflict and busy session.
- The audit reopens artifacts in order, verifies facts, relations and samples,
  and keeps the first invalid point without deleting earlier diagnostics.
- An interrupted run is recovered as `incomplete`; its checkpoint is `absent`,
  `verified` or `unreadable`. `automatic_resume_allowed` is always `false`:
  recovery never relaunches the take.

## Compatibility

- Containers, PCM, checkpoints, ledger and references declare their versions.
  Fact records accept only 1.0–1.4.
- An unsupported version, truncation, excess, hash mismatch or unsafe path
  returns an explicit error without migrating or overwriting the original. A
  future migration must create a new view.
- Runtime negotiates the Engine, Runtime QA, evidence and HD-state contracts
  separately. All four must offer 1.0 and the ten mandatory capabilities. An
  incompatible Runtime yields `runtime_incompatible` and is not credited with a
  replay.
- A request with pad timelines also requires `timeline_replay_v1`; without it
  the request is rejected with `runtime_missing_timeline_replay_v1` before
  admission. A request with `--at-end close` requires `close_at_end_v1`;
  without it, `runtime_missing_close_at_end_v1`.

## Isolation and private materials

- Preflight computes the SHA-256 of every material (Runtime, ROM, core, pack,
  takes, manifest and trust registry) and stores it in the request summary.
- During each take, the checker keeps that take's materials open with
  `FILE_SHARE_READ`, without write or delete sharing, and checks each SHA-256
  against the preflight value before handing it to Runtime. If any material
  changed, the take does not start and the request stops with
  `material_changed`, keeping earlier evidence.
- Each query and replay gets a private Runtime data root under the campaign
  output. The checker snapshots the user's ordinary data tree, verifies it is
  unchanged at the end and removes the private root (RAII). A change yields
  `user_data_changed`; an isolation that cannot be prepared yields
  `runtime_data_isolation_failed`.
- Evidence keeps identities and provenance, never ROM or core bytes. Pack,
  takes, manifest, trust registry and ordinary saves are read-only inputs. The
  final campaign also verifies that no private roots remain and that no
  deliverable file matches the ROM or core hashes.
