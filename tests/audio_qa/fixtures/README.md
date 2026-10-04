# Public audio-QA integration fixtures

These files contain only generated AYTHER test data:

- `public-synthetic.aytest` is the 16-byte public ROM payload used by the
  synthetic libretro core.
- `public-tone.wav` is a 16-frame, stereo, 16-bit PCM ramp at 44.1 kHz.
- `public-synthetic.ay` maps the deterministic FM event emitted for the
  recording inputs to that PCM asset.
- `public-synthetic-trust.toml` trusts only the reproducible test key and only
  for `ayther-public-synthetic-v1`.
- `public-synthetic-poses.ay` is the same signed pack with one pose and a valid
  1x1 PNG, and `public-synthetic-corrupt-asset.ay` the same pose with a PNG whose
  image data is not a zlib stream; both are listed and signed in the index, so
  only decoding finds the problem (spec 002, pack probe).
- `public-synthetic-visual.ay` is the same signed pose pack without audio catalog,
  the valid visual-only pack that spec 002 replays with zero audio assignments.

`generate_public_fixtures.py` rebuilds all four artifacts. Its signing key is
derived from a public label and is explicitly unsuitable for production. CTest
uses the checked-in artifacts and therefore does not require Python or the
`cryptography` package.
