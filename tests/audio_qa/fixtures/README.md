# Public audio QA fixtures

These files contain only generated AYTHER test data.

| File | Content |
| --- | --- |
| `public-synthetic.aytest` | 16-byte public ROM payload for the synthetic libretro core. |
| `public-tone.wav` | 16-frame stereo 16-bit PCM ramp at 44.1 kHz. |
| `public-synthetic.ay` | Signed pack mapping the deterministic FM event of the recording inputs to that PCM asset. |
| `public-synthetic-trust.toml` | Trusts only the reproducible test key, only for `ayther-public-synthetic-v1`. |
| `public-synthetic-poses.ay` | The same signed pack with one pose and a valid 1×1 PNG. |
| `public-synthetic-corrupt-asset.ay` | The same pose with a PNG whose image data is not a zlib stream. It is listed and signed in the index, so only decoding finds the problem (pack probe test). |
| `public-synthetic-visual.ay` | The signed pose pack without an audio catalog: a valid visual-only pack replayed with zero audio assignments. |
| `rf1-option-inventory-beta8.toml` | Frozen `check` option inventory; schema of `ayther_audio_qa options --format toml` and reference for the launcher's option coverage test. |

`generate_public_fixtures.py` rebuilds every file above except
`rf1-option-inventory-beta8.toml`. Its signing key is derived from a public
label and is unsuitable for production. CTest uses the checked-in files and
needs neither Python nor the `cryptography` package.
