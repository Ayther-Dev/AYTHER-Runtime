# Signed packs and trust registries

Runtime beta.3 connects `--trust-registry <file.toml>` to Engine's
`AytherSession::Config::trust_registry` (rc.6 onward; the lock now pins rc.8).
Relative paths resolve against the launch working directory before SDL or session creation. Quote paths containing
spaces. AYTHER Play must pass this option explicitly; `--manifest` remains
session metadata and does not supply configuration implicitly.

```powershell
ayther_runtime.exe --core "C:\cores\core.dll" --rom "C:\roms\game.md" `
  --pack "C:\packs\game.ay" --trust-registry "C:\config\trust.toml" --frames 600
```

The registry contains **public keys only**. Provision it through your trusted
distribution channel; a registry supplied by an untrusted pack does not establish
trust. The following is a format template, not a usable production registry:

```toml
version = 1

[[keys]]
id = "publisher-2026-01"
algorithm = "ed25519"
public_key = "<64 lowercase hexadecimal characters from the publisher>"
not_before_unix = 1767225600
not_after_unix = 1830297600
revoked = false
games = ["crc32:665d7df9"]
```

Identifiers contain 1–64 ASCII letters, digits, `.`, `_` or `-` and must be
unique. `games` is a nonempty array of exact manifest game IDs; IDs admit ASCII
letters, digits, `.`, `_`, `-` and `:`. `"*"` explicitly delegates all games.
Validity bounds are inclusive Unix seconds. `revoked` defaults to false.
`version = 1` with no keys is an empty store and trusts no production signers.
Unknown fields, unsupported algorithms, malformed hex keys and invalid field
types are rejected. Never put private keys in this file.

Runtime checks file accessibility, TOML and the version-1 field structure.
Engine remains authoritative for Ed25519 key and signature validation, current
validity, revocation, integrity and authorization of the authenticated manifest's
game ID. Scope does not itself prove that a ROM matches a pack; Engine's separate
compatibility checks still apply. Runtime does not override those checks.

The absolute registry path survives pack reloads. Engine reads its current
contents when reopening the pack. Runtime uses `set_pack` with its own path copy
to avoid rc.6's `reload_pack` path-aliasing issue. Changing the registry alone does
not trigger a reload: restart the session or trigger a pack reload to apply an
updated revocation list.

Missing, unreadable or malformed registry configuration emits
`pack.trust_registry_invalid` and exits **78**. Missing/empty option values exit
**64**. An explicit pack that cannot open, validate, activate or reload emits
`pack.open_failed` and exits **4**. No initial `ready` event is emitted for a
failed explicit pack; a failed reload terminates the running session without a
successful `exit` event. The diagnostic identifies the pack and trust registry;
rc.6's public inspection API does not expose a separate reason for each signature
or key-policy failure.
Launchers must check the process status even when an error is carried by the
protocol-v1 event named `warning`.

Omitting `--pack` still permits original-ROM play and legacy convention discovery.
Omitting the registry retains Engine's authoring policy: the distributed Release
Engine rejects both unsigned packs and the public development signing key.
The fixtures under `tests/trust` are **test-only** and must never be provisioned
as production trust. They require no network or private signing service.

For acceptance, require successful process exit, `has_pack=1`, and the requested
600-frame completion log. `video_decoded_frames` counts changes to decoded video
frames delivered by Engine; require a positive count with VP9 test content.
The Windows beta.3 package uses the locked Engine rc.6 **engine-vpx** artifact.
The Linux package retains the non-VPX Engine artifact; it does not claim VP9.
