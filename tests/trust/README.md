# Test-only trust fixtures

These packs contain synthetic metadata only. `generate_fixtures.py` rebuilds
them deterministically using a publicly reproducible, Runtime-specific Ed25519
test seed. The separate development fixture uses Engine's public RFC 8032 seed.
Neither key is suitable for distribution or production trust.

CTest consumes the checked-in files without Python or cryptography. Regeneration
requires Python and `cryptography`. `acceptance.ivf` is synthetic FFmpeg testsrc2
content (64×64, 30 frames, VP9, every frame a keyframe), generated with:

```text
ffmpeg -f lavfi -i testsrc2=size=64x64:rate=30 -frames:v 30 -c:v libvpx-vp9 -g 1 -pix_fmt yuv420p acceptance.ivf
```

`prepare_acceptance.py` is an explicit local-only helper for the user-supplied
Golden Axe input checksum. It verifies the original index and development
signature, then writes separately named test-signed copies under an ignored
output directory. It never modifies the input or uses production keys.
