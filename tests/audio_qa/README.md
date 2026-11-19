# Audio QA tests

This directory holds the CTest suite of the audio QA checker
([tools/audio_qa](../../tools/audio_qa/README.md)) and the known-PCM fixture
generator. It is built as part of the normal Runtime tree; the tests that need
the QA Engine package run only with the `windows-qa` presets.

```powershell
ctest --preset windows-qa -L audio_qa -LE gpu --output-on-failure
```

Public fixtures are described in [fixtures/README.md](fixtures/README.md).

## Known PCM block (`known-pcm-v1`)

`audio_qa_known_pcm_fixture` writes a synthetic, identified PCM block used as
known input by capture tests. It links neither Engine, Runtime nor SDL and
opens no device.

- Interleaved S16LE, 44 100 Hz, two channels in left/right order.
- 1024 sample frames, input range `[0, 1024)`, 4096 bytes, no WAV header. They
  are not device output indices or game frames.
- The first four frames are `(-32768, 32767)`, `(0, -1)`, `(8192, -4096)` and
  `(-16384, 16384)`.
- For each index *i* from 4 to 1023: left = −8192 + 16*i*; right = 4096 − 4*i*.
  Last frame: `(8176, 4)`.
- SHA-256 of the bytes:
  `5d764a90b6ae1b6606aa95bbd343c54fd1a7b3aa7f7f20e116afc53423081602`.

The markers check sign, extremes, channel order and endianness; the ramps
reveal offsets without an external clock. The expected hash is fixed
independently of the C++ generator; changing the signal requires a new fixture
identity. `known_pcm.h` also provides the typed block, without I/O.

The executable takes one new output path, writes the PCM and prints its TOML
metadata on stdout. It refuses to replace existing files and is meant for
controlled test directories without concurrent writers.

```powershell
ctest --preset windows-qa -R "^audio_qa_known_pcm_" --output-on-failure
```

| Test | Checks |
| --- | --- |
| `audio_qa_known_pcm_block` | Runs the generator twice; format, size and hash against fixed values. |
| `audio_qa_known_pcm_errors` | Rejects missing arguments, an unwritable destination and an existing file, keeping a sentinel file. |

Each run keeps a new subdirectory under `results/<config>/` of the build tree
with `first.toml`, `first.s16le.pcm`, `repeat.s16le.pcm` and their manifests. A
manifest is written only after the PCM is reread and verified. These results
are regenerable fixtures, not transactional evidence; a partial file left by an
interrupted write is never reused as a valid result.
