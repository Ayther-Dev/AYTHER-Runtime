# Windows x86-64 acceptance — beta.3

Validated on 2026-09-09 with MSVC v145 14.51.36231, Windows
10.0.26200, Vulkan on NVIDIA GeForce RTX 3060 Laptop GPU, and Engine
`v0.1.0-rc.6` **engine-vpx**. The Engine ZIP SHA-256 matches the dependency lock:
`73a0b2c55e7f9f688339dfe17355f7a3d65c2b076ae2cd7ef21654ab525feb50`.

## Verified user inputs

| Input | SHA-256 |
| --- | --- |
| Golden Axe (World) (Rev A).md | `e9f5340ecf8151253eb6fcda136c4d4d8940e373340ce2eeb2bf24f9f6c1004d` |
| golden-axe.ay (original, unchanged) | `affd0a07a5560745acf5a65c71ffab05e5dfd84ef2ab59352db5af2169ddddce` |
| genesis_plus_gx_libretro_ayther_x64.dll | `4f7048733a465b26e7c5944d61bccf9e77310201902a82fce32da46c736e870d` |

The original pack declares Engine `0.8.0` and has a legacy development
signature. It is not a release-compatible production-signed pack. Its integrity
index and development signature were verified before creating local copies.
The original bytes were preserved.

`tests/trust/prepare_acceptance.py` creates an rc.6 copy by changing the manifest
requirement to `0.1.0`, rebuilding the integrity index and signing with the
Runtime-specific **test-only** key. Other original content bytes are preserved.
The VP9 copy additionally carries synthetic 64×64, 30-frame all-keyframe VP9
content, with screen and kinematic definitions captured at frames 120 and 240
by `capture_acceptance_screens`. These copies are local acceptance artifacts,
not production-approved content; neither they nor the ROM/core are published.

| Acceptance pack | SHA-256 | Result |
| --- | --- | --- |
| golden-axe-trusted.ay | `2fa74061bf8ffb447b89862e602632fa22ef787cc19190caa26f4adc26ea6c17` | `has_pack=1`, 600 frames, exit 0 |
| golden-axe-vp9-test.ay | `1fcd302663f6f0a829e3edaef2f96d72f99bba2c8fc0c4e84978f4fe4f829c8a` | `has_pack=1`, 600 frames, exit 0, 57 decoded video frames |

Both were run with `tools/validate_signed_playback.ps1`, first from the build
and then from the installed RelWithDebInfo package, with identical outcomes.
The complete CTest suite passed **50/50**. An additional explicit **Release**
build passed all three selected trust/CLI/options tests, including development
signature rejection. The acceptance script checks all three
input hashes before launching, records stdout/stderr and runtime/registry hashes,
and rejects a zero-status original-ROM fallback. The VP9 run uses `-RequireVp9`;
merely loading the VPX library or including a video entry cannot pass it.
Logs and JSON evidence remain under ignored `out/acceptance-beta3`.

The existing content emits an audio asset degradation diagnostic for
`ef811067957c5d3c8d5045776acb2bc1`. The run establishes trusted activation,
frame completion and actual VP9 decoding; it does not certify all original
replacement audio assets or audiovisual fidelity.

The automated trust matrix covers valid signatures, unknown/revoked/expired/
not-yet-valid keys, unauthorized games, altered packs, missing signatures,
development signatures rejected by the Release Engine, missing/unreadable/
malformed registries, relative CLI paths, activation and revocation on reload.
Tests consume the installed Engine public API and checked-in synthetic fixtures.
