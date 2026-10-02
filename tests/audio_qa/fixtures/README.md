# Public audio-QA integration fixtures

These files contain only generated AYTHER test data:

- `public-synthetic.aytest` is the 16-byte public ROM payload used by the
  synthetic libretro core.
- `public-tone.wav` is a 16-frame, stereo, 16-bit PCM ramp at 44.1 kHz.
- `public-synthetic.ay` maps the deterministic FM event emitted for the
  recording inputs to that PCM asset.
- `public-synthetic-trust.toml` trusts only the reproducible test key and only
  for `ayther-public-synthetic-v1`.

`generate_public_fixtures.py` rebuilds all four artifacts. Its signing key is
derived from a public label and is explicitly unsuitable for production. CTest
uses the checked-in artifacts and therefore does not require Python or the
`cryptography` package.
