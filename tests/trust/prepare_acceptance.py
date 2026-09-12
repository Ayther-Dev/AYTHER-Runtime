"""Create TEST ONLY rc.6 and VP9 copies of the verified Golden Axe pack.

Usage: prepare_acceptance.py ORIGINAL OUTPUT_DIR ELEMENTS_TOML VIDEO_IVF
Preserves the original. Requires cryptography; never uses production keys.
"""
import sys
import json
import tomllib
import hashlib
import zipfile
from pathlib import Path
from generate_fixtures import KEY, KEY_ID, DEV, PUBLIC

original, destination, elements, video = map(Path, sys.argv[1:])
expected = "affd0a07a5560745acf5a65c71ffab05e5dfd84ef2ab59352db5af2169ddddce"
assert hashlib.sha256(original.read_bytes()).hexdigest() == expected
destination.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(original) as archive:
    files = {name: archive.read(name) for name in archive.namelist()}
DEV.public_key().verify(files["signature.bin"], files["integrity.toml"])
for entry in tomllib.loads(files["integrity.toml"].decode())["entry"]:
    data = files[entry["path"]]
    assert len(data) == entry["size"] and hashlib.sha256(data).hexdigest() == entry["sha256"]
files.pop("integrity.toml")
files.pop("signature.bin")
files["manifest.toml"] = files["manifest.toml"].replace(b'ayther_min = "0.8.0"', b'ayther_min = "0.1.0"')
game = tomllib.loads(files["manifest.toml"].decode())["pack"]["game_id"]
(destination / "trust.toml").write_text(f'''version = 1
[[keys]]
id = "{KEY_ID}"
algorithm = "ed25519"
public_key = "{PUBLIC}"
not_before_unix = 0
not_after_unix = 4102444800
revoked = false
games = ["{game}"]
''', encoding="utf-8")


def write_pack(name, content):
    integrity = "version = 1\n"
    for path, data in sorted(content.items()):
        integrity += (f'[[entry]]\npath = {json.dumps(path)}\nsha256 = "{hashlib.sha256(data).hexdigest()}"\n'
                      f'size = {len(data)}\n')
        if data:
            chunk = max(65536, (len(data) + 2047) // 2048)
            hashes = [hashlib.sha256(data[i:i+chunk]).hexdigest() for i in range(0, len(data), chunk)]
            integrity += f'chunk = {chunk}\nchunks = {json.dumps(hashes)}\n'
    integrity = integrity.encode()
    envelope = b"AYTHSIG\0\x01" + bytes([len(KEY_ID)]) + KEY_ID.encode() + KEY.sign(integrity)
    with zipfile.ZipFile(destination / name, "w") as archive:
        for path, data in sorted({**content, "integrity.toml": integrity, "signature.bin": envelope}.items()):
            archive.writestr(zipfile.ZipInfo(path, (2026, 1, 1, 0, 0, 0)), data)
    return hashlib.sha256((destination / name).read_bytes()).hexdigest()


hashes = {"original.ay": expected, "golden-axe-trusted.ay": write_pack("golden-axe-trusted.ay", files)}
files["elements.toml"] = files.get("elements.toml", b"") + elements.read_bytes()
files["video/acceptance.ivf"] = video.read_bytes()
hashes["golden-axe-vp9-test.ay"] = write_pack("golden-axe-vp9-test.ay", files)
(destination / "inputs.json").write_text(json.dumps(hashes, indent=2), encoding="utf-8")
print(json.dumps(hashes, indent=2))
