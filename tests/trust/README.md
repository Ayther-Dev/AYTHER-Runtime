# Test-only trust fixtures

These packs and registries contain synthetic metadata only and back the
[trust registry](../../docs/trust-registry.md) tests. **Never provision them as
production trust.**

- `generate_fixtures.py` rebuilds them deterministically from a publicly
  reproducible, Runtime-specific Ed25519 test seed. The separate development
  fixture (`development.ay`) uses Engine's public RFC 8032 seed. Neither key is
  suitable for distribution.
- CTest consumes the checked-in files without Python. Regeneration needs Python
  and `cryptography`.
- `acceptance.ivf` is synthetic FFmpeg `testsrc2` content (64×64, 30 frames,
  VP9, every frame a keyframe):

  ```text
  ffmpeg -f lavfi -i testsrc2=size=64x64:rate=30 -frames:v 30 -c:v libvpx-vp9 -g 1 -pix_fmt yuv420p acceptance.ivf
  ```

- `prepare_acceptance.py` is a local-only helper for a user-supplied Golden Axe
  input checksum. It verifies the original index and development signature,
  then writes separately named test-signed copies under an ignored output
  directory. It never modifies the input or uses production keys.
