# FX-02 — active-audio Mega Drive ROM

`manifest.json` identifies fixture `FX-02-megadrive-audio-active-v1`: a 32 KiB
ROM (`program.md`, SHA-256
`5296c4fa331fd8cb47663fc7ce3b0abef03364221b309a65f2966c6a8642225c`) emitted by
AYTHER Tessera's `tests/backend_probe/main.cpp` (MIT), with its traces, oracle
and observable windows. Source and oracle live in Tessera's
`tests/fixtures/audio-active/`.

The ROM is not distributed; a local copy here is ignored by git. The Runtime
tests read it from `${AYTHER_TESSERA_TEST_FIXTURE_DIR}/fx02/program.md`, and
are configured only with `AYTHER_ENABLE_TESSERA_NATIVE=ON`.

| Test | Checks |
| --- | --- |
| `tessera_runtime_live_input` | A virtual SDL gamepad drives the Tessera session through `GameInput` and `set_input`. |
| `tessera_runtime_present` (GPU) | Shaders on and off, and capture, with the native provider. |
| `tessera_runtime_active_audio` (GPU) | B held for 180 frames yields non-silent original PCM in the SDL stream. |

The neutral/active/neutral transition, state, video and hash oracles are
verified in Tessera and Engine.
