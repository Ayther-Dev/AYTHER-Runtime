# FX-03 — synthetic graphics-replacement pack

`manifest.json` records the SHA-256 of the ROM (`fx03_rom.md`), the signed pack
(`fx03.ay`), its trust registry (`trust.toml`) and the two assets. The
generator fixes the pack's `game_id` to the ROM's CRC32 (`crc32:2af4a80d`).

The binaries are not published; local copies here are ignored by git. They are
generated in the AYTHER Tessera checkout with `tools/generate-native-fixtures.ps1`,
after building Tessera and Engine's `make_test_pack`; the sources are in
Tessera's `tests/fixtures/graphics-replacement/`. The Runtime tests read them
from `${AYTHER_TESSERA_TEST_FIXTURE_DIR}/fx03/` and are configured only with
`AYTHER_ENABLE_TESSERA_NATIVE=ON`.

## Oracle

- Two 8×8 images, original patterns made for this test (CC0-1.0, author AYTHER
  Tessera).
- The rule targets the measured hash of the demo's visible red tile at
  `(32,32)`–`(39,39)` and of the plane A cell at `(96,64)`–`(103,71)`.
- Two identical `[[sub]]` entries: the catalog must choose `first.png`
  (magenta); a runtime override on the same hash must choose `second.png`
  (cyan). The original Tessera frame stays red.
- The candidate change mask is only those two cells. The second cell has
  priority over a green sprite in SAT slot 0. A blue sprite in slot 1 starts at
  x=104: slot 0 wins at x=105 despite slot 1's graphic priority, and slot 1 is
  visible at x=109.
- `manifest.json` fixes those four original pixels and the expected ones with
  catalog and override: magenta or cyan in both cells, green at x=105, blue at
  x=109; outside the two masks, bytes identical to the original.

## Tests

| Test | Checks |
| --- | --- |
| `tessera_runtime_native_headless` | Runtime plays the ROM with the signed pack through `--provider tessera` without a window. |
| `tessera_runtime_gpu_audio` (GPU) | The same material with the audio device. |
| `tessera_runtime_pack` (GPU, optional) | A pack baked with gpgx on FX-03 (`AYTHER_TESSERA_CROSS_PACK_DIR`) played with Tessera. |

Engine's `ayther.integration.tessera_presentation_test` separately verifies
composition, override, fallback, recovery and preservation of original
state, video and PCM.
