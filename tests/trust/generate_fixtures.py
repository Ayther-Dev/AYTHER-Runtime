"""Rebuild public, deterministic TEST ONLY fixtures (requires cryptography).

No production key is read or generated. Checked-in packs let CTest run offline
without Python or a signing library. ZIP timestamps and entries are fixed.
"""
from pathlib import Path
import hashlib
import zipfile
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat

ROOT = Path(__file__).resolve().parent
KEY_ID = "runtime-test-only"
KEY = Ed25519PrivateKey.from_private_bytes(hashlib.sha256(b"Runtime trust fixtures ONLY v1").digest())
PUBLIC = KEY.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw).hex()
DEV = Ed25519PrivateKey.from_private_bytes(bytes.fromhex(
    "4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8c f6ed4d0bd6d9".replace(" ", "")))
MANIFEST = b'[pack]\nname = "Runtime Trust Test"\nversion = "1.0.0"\ngame_id = "runtime-test"\nayther_min = "0.1.0"\n[regions]\ndefault = "NTSC"\nsupported = ["NTSC"]\n'


def registry(name, *, key_id=KEY_ID, before=0, after=4102444800, revoked=False, games='"runtime-test"'):
    (ROOT / name).write_text(f'''version = 1
[[keys]]
id = "{key_id}"
algorithm = "ed25519"
public_key = "{PUBLIC}"
not_before_unix = {before}
not_after_unix = {after}
revoked = {str(revoked).lower()}
games = [{games}]
''', encoding="utf-8", newline="\n")


def pack(name, *, key=KEY, key_id=KEY_ID, tampered=False, manifest=MANIFEST):
    integrity = (f'version = 1\n[[entry]]\npath = "manifest.toml"\n'
                 f'sha256 = "{hashlib.sha256(manifest).hexdigest()}"\nsize = {len(manifest)}\n').encode()
    entries = {"manifest.toml": manifest.replace(b"Trust Test", b"Trust FAKE") if tampered else manifest,
               "integrity.toml": integrity}
    if key:
        entries["signature.bin"] = b"AYTHSIG\0\x01" + bytes([len(key_id)]) + key_id.encode() + key.sign(integrity)
    with zipfile.ZipFile(ROOT / name, "w") as archive:
        for name, data in entries.items():
            archive.writestr(zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0)), data)


if __name__ == "__main__":
    registry("valid.toml")
    registry("unknown.toml", key_id="another-test-key")
    registry("revoked.toml", revoked=True)
    registry("expired.toml", after=1)
    registry("future.toml", before=4102444799)
    registry("wrong-game.toml", games='"another-game"')
    pack("valid.ay")
    pack("tampered.ay", tampered=True)
    pack("unsigned.ay", key=None)
    pack("development.ay", key=DEV, key_id="ayther-development-rfc8032")
    pack("incompatible.ay", manifest=MANIFEST.replace(b'"0.1.0"', b'"99.0.0"'))
