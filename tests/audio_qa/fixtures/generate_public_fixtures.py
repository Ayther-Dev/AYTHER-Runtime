"""Rebuild the public deterministic audio-QA pack (requires cryptography).

The private key is derived from a published test label. It has no production
value. CTest consumes the checked-in outputs and does not execute this script.
"""

from pathlib import Path
import hashlib
import zipfile

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat


ROOT = Path(__file__).resolve().parent
KEY_ID = "audio-qa-public-test-only"
KEY = Ed25519PrivateKey.from_private_bytes(
    hashlib.sha256(b"AYTHER public audio QA fixture v1").digest()
)
PUBLIC = KEY.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw).hex()
GAME_ID = "ayther-public-synthetic-v1"
SIGNATURE = "daf281b060d3fe48"
ROM = b"AYTHER-PUBLIC-QA"
WAV = bytes(
    [
        82, 73, 70, 70, 52, 0, 0, 0, 87, 65, 86, 69, 102, 109, 116, 32,
        16, 0, 0, 0, 1, 0, 2, 0, 68, 172, 0, 0, 16, 177, 2, 0,
        4, 0, 16, 0, 100, 97, 116, 97, 16, 0, 0, 0, 1, 0, 1, 0,
        2, 0, 2, 0, 3, 0, 3, 0, 4, 0, 4, 0,
    ]
)
ASSET_ID = hashlib.sha256(WAV).hexdigest()[:32]
MANIFEST = f'''[pack]
name = "AYTHER Public Audio QA"
version = "1.0.0"
game_id = "{GAME_ID}"
ayther_min = "0.1.0"
[regions]
default = "NTSC"
supported = ["NTSC"]
'''.encode()
CATALOG = f'''[[event]]
signature = "0x{SIGNATURE}"
asset = "{ASSET_ID}"
looping = false
'''.encode()


def integrity(entries: dict[str, bytes]) -> bytes:
    text = "# integrity.toml — per-entry hashes; signature.bin signs these exact bytes\nversion = 1\n"
    for path in sorted(entries):
        data = entries[path]
        text += (
            f'\n[[entry]]\npath   = "{path}"\n'
            f'sha256 = "{hashlib.sha256(data).hexdigest()}"\n'
            f"size   = {len(data)}\n"
        )
    return text.encode()


def main() -> None:
    asset_path = f"assets/{ASSET_ID}"
    entries = {
        "audio_events.toml": CATALOG,
        asset_path: WAV,
        "manifest.toml": MANIFEST,
    }
    index = integrity(entries)
    envelope = b"AYTHSIG\0\x01" + bytes([len(KEY_ID)]) + KEY_ID.encode() + KEY.sign(index)
    complete = {**entries, "integrity.toml": index, "signature.bin": envelope}
    with zipfile.ZipFile(ROOT / "public-synthetic.ay", "w") as archive:
        for path in sorted(complete):
            info = zipfile.ZipInfo(path, (2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, complete[path])

    (ROOT / "public-synthetic.aytest").write_bytes(ROM)
    (ROOT / "public-tone.wav").write_bytes(WAV)
    (ROOT / "public-synthetic-trust.toml").write_text(
        f'''version = 1
[[keys]]
id = "{KEY_ID}"
algorithm = "ed25519"
public_key = "{PUBLIC}"
not_before_unix = 0
not_after_unix = 4102444800
revoked = false
games = ["{GAME_ID}"]
''',
        encoding="utf-8",
        newline="\n",
    )


if __name__ == "__main__":
    main()
