# Replay QA launcher (`ayther_replay_qa`)

`ayther_replay_qa` is a window for preparing and starting a replay QA check
without typing the `ayther_audio_qa check` command line. It uses the same
library (`run_check`) and the same option table: every `check` option has a
field, and none is left out. Options and rules are documented in
[tools/audio_qa](../audio_qa/README.md).

It is installed with the `qa` component, next to `ayther_audio_qa`, never with
the end-user Runtime component.

The interface is bilingual. UI labels are quoted below in English with the
Spanish label (the default language) in parentheses.

## Usage

1. **Open `ayther_replay_qa.exe`.** The form starts empty: it does not remember
   ROM, takes or pack from a previous session. The initial mode is visible
   presentation.
2. **Choose the materials** with "Choose…" («Elegir…»), which opens the native
   file dialog.
   - **ROM:** the game.
   - **Takes:** one or more `.ayr` files. They can be reordered, repeated and
     removed; each repeat is another position.
   - **Pad timelines:** one or more `.timeline` text files that drive the pad
     instead of a take, from power-on or a Lab bookmark. Same operations as
     takes; they run after them. At least one take or timeline is required;
     otherwise both fields say "A take or a pad timeline is required" («Hace
     falta una toma o un guion de mando»). A rejected timeline shows its code at
     its position and, below the errors, the line and reason
     (`audio_qa_diagnostic`). Grammar:
     [pad timelines](../audio_qa/README.md#pad-timelines).
   - **Pack:** optional. "Remove" («Quitar») clears it, and the summary then
     says "No pack" («Sin pack»).
   - **Runtime and core:** if a Runtime is installed next to the launcher and a
     Play CE configuration exists, they come from there; the summary shows the
     origin "environment" («entorno»). The Play CE configuration is only read.
   - **Pasted paths:** a path wrapped in double quotes (as Windows "Copy as
     path" produces) is used without them in every path field, and is recorded
     without them.
3. **Review the sections:** environment and conditions, auxiliary information,
   destination and identity, presentation and language. An invalid field shows
   its error below it, for example "Required field" («Campo obligatorio») or a
   preflight code such as `core_platform_mismatch`.
   - **At the end** («Al terminar», `--at-end`): `inspect` keeps the Runtime
     window paused on the last frame at the natural end; `close` closes it once
     the result is sent. `close` requires a Runtime with `close_at_end_v1`.
4. **Check the effective-values summary.** Each row shows its origin: selected,
   default, environment or manifest. Any change revalidates the request with
   Runtime; "Start" («Iniciar») is enabled only when no error remains.
5. **Press "Start" («Iniciar»).** The request runs on a worker thread and the
   window stays responsive. The phase line shows validation, admission,
   preparation, playback and close, with the current take.
6. **To stop, press "Cancel" («Cancelar»).** The request is cancelled; takes
   that had not started are reported as not started.
7. **Read the results:** per take, playback, traversal and evidence, with the
   reasons for incomplete evidence, then the joint result. A technical result is
   not a visual evaluation.

### Language

The "Language" («Idioma») field is the `--language` option: `es` (default) or
`en`. The same language reaches the Runtime replay window: hints, debug overlay
and messages.

### Replay window

With visible presentation, Runtime opens its own window per take: Space pauses
and resumes, Left/Right step in pause, `I` toggles the debug record, Esc
closes. The status line, event history and natural-end behavior are described
in [docs/audio-qa-evidence.md](../../docs/audio-qa-evidence.md#inspection-controls);
the key table is in [docs/input-map.md](../../docs/input-map.md#replay-qa-window).

## Build

The launcher is part of the Runtime QA build and needs the QA Engine package
(see [docs/development.md](../../docs/development.md), «Replay QA»):

```powershell
$env:AYTHER_ENGINE_PREFIX = & ./tools/bootstrap_ayther_engine_qa.ps1
cmake --preset windows-qa
cmake --build --preset windows-qa --target ayther_replay_qa
```

- **Dependencies:** SDL3 and Dear ImGui with its SDL_Renderer backend, so
  `vcpkg.json` enables the `imgui` feature `sdl3-renderer-binding` in addition
  to `sdl3-binding` and `vulkan-binding`. The launcher window draws with
  SDL_Renderer and does not need Vulkan.
- **Structure:** the model is pure and built separately as
  `ayther_replay_qa_model` (form, validation, sections, summary, results and
  texts). The window (`launcher_app.cpp`) only lays it out.

## Tests

Without GPU:

```powershell
ctest --preset windows-qa -R "^replay_qa_launcher_" --output-on-failure
```

| Test | Covers |
| --- | --- |
| `replay_qa_launcher_launcher_messages` | es/en catalog. |
| `replay_qa_launcher_launcher_model` | Fields, required values, «Sin pack», ordered takes, revalidation, phases and results. |
| `replay_qa_launcher_environment_resolver` | Runtime and core from the environment. |
| `replay_qa_launcher_launcher_runner` | Worker thread and cancellation. |
| `replay_qa_launcher_launcher_view` | Dialogs, sections, per-type validation, summary, errors and results. |
| `replay_qa_launcher_option_coverage` | One field per option; regenerates `evidence/rf1-option-correspondence.md` in the build tree. |

With GPU and a desktop session (`windows-qa-gpu`):

```powershell
ctest --preset windows-qa-gpu -R "^replay_qa_launcher_(window_smoke|interface_smoke|idle_cpu)$" --output-on-failure
```

| Test | Command and check |
| --- | --- |
| `replay_qa_launcher_window_smoke` | `ayther_replay_qa --smoke-frames 30` opens the window and draws 30 frames; `--smoke-capture <file.bmp>` saves the last one. |
| `replay_qa_launcher_interface_smoke` | `ayther_replay_qa --self-test`: shows the required-field errors, starts a visible request with the synthetic core, cancels it after two seconds of playback, and shows the take as cancelled («Cancelada») plus the joint result. Materials come from `--runtime`, `--core`, `--rom`, `--take` (or `--timeline`) and `--output`. |
| `replay_qa_launcher_idle_cpu` | `ayther_replay_qa --idle-seconds 3` idles three seconds and reports redraws and CPU. Idle, the window waits for input (or redraws progress every 100 ms) with vsync; the test allows at most one redraw per refresh and less than a quarter of a core. |

`--self-test`, `--smoke-frames`, `--smoke-capture` and `--idle-seconds` are test
modes, not normal use.

## Prefilled fields

`ayther_replay_qa --prefill <file>` opens the form with the fields of the file:
one `--field=value` line per value; `--take`, `--timeline` and `--core-option`
repeat in order. All values count as explicit, and absent fields stay
unselected. An unknown or malformed line rejects the whole file
(`prefill_rejected` in the log) and the form stays empty. The spec 002 campaign
tool uses it.
