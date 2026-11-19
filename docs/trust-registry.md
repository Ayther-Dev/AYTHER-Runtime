# Signed packs and trust registries

`--trust-registry <file.toml>` gives Engine the public keys that may sign AYTHER
packs. Runtime passes the file to `AytherSession::Config::trust_registry`;
Engine makes every trust decision. This page is for launcher authors and pack
publishers.

## Usage

```powershell
ayther_runtime.exe --core "<core.dll>" --rom "<game.md>" `
  --pack "<game.ay>" --trust-registry "<trust.toml>"
```

- A relative path resolves once against the launch working directory, before
  SDL or session creation. Quote paths that contain spaces.
- AYTHER Play must pass the option explicitly. `--manifest` is session metadata
  and never supplies configuration.
- Without `--trust-registry`, Engine applies its authoring policy: the
  distributed Release Engine rejects both unsigned packs and the public
  development signing key.
- Without `--pack`, original-ROM play and convention-based pack discovery are
  unchanged.

## Format

The registry contains **public keys only**. Provision it through a trusted
distribution channel; a registry supplied by an untrusted pack establishes no
trust. Never put private keys in this file. This example is a format template,
not a usable registry:

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

| Field | Rule |
| --- | --- |
| `version` | Integer `1`. A file with `version = 1` and no keys is an empty store that trusts no signer. |
| `keys[].id` | 1–64 ASCII letters, digits, `.`, `_` or `-`. Unique. |
| `keys[].algorithm` | `ed25519`. |
| `keys[].public_key` | 64 lowercase hexadecimal characters. |
| `keys[].not_before_unix`, `keys[].not_after_unix` | Required integers; inclusive Unix seconds. `not_before_unix` ≥ 0 and `not_after_unix` ≥ `not_before_unix`. |
| `keys[].revoked` | Boolean; default `false`. |
| `keys[].games` | Non-empty array of exact manifest game IDs (ASCII letters, digits, `.`, `_`, `-`, `:`), or `"*"` to delegate all games. |

Unknown fields, unsupported algorithms, malformed keys and wrong field types are
rejected.

## Responsibilities

- **Runtime** checks that the file is readable, is TOML and matches the
  version-1 structure above.
- **Engine** validates Ed25519 keys and signatures, current validity,
  revocation, integrity and whether the key authorizes the authenticated
  manifest's game ID. Key scope does not prove that a ROM matches a pack;
  Engine's separate compatibility checks still apply. Runtime never overrides
  them.

The absolute registry path is kept for pack reloads, and Engine reads the file's
current contents each time it reopens the pack. Changing the registry alone does
not trigger a reload: restart the session or reload the pack to apply an updated
revocation list.

## Failures

| Condition | Reason | Exit |
| --- | --- | ---: |
| Option given without a value, or with an empty value | `cli.invalid_argument` | 64 |
| Registry missing, unreadable or malformed | `pack.trust_registry_invalid` | 78 |
| Explicit pack cannot open, validate, activate or reload | `pack.open_failed` | 4 |

- No `ready` event is emitted for a failed explicit pack. A failed reload ends
  the session without a successful `exit` event.
- The diagnostic names the pack and the registry. Engine's public inspection
  API does not report a separate reason for each signature or key-policy
  failure.
- Check the process exit code even when the failure arrives as a `warning`
  event; see [runtime-play-protocol.md](runtime-play-protocol.md).

## Test fixtures and acceptance

The fixtures under [`tests/trust`](../tests/trust/README.md) are **test-only**
and must never be provisioned as production trust. They need no network or
private signing service.

Acceptance for signed playback: successful exit, `has_pack=1`, and the
completion log of a `--frames 600` run. `video_decoded_frames` in that log
counts decoded video frames delivered by Engine; require a positive count with
VP9 content. VP9 needs the `engine-vpx` Engine variant, which the Windows
release package uses; the Linux release package uses the plain `engine` variant
and does not claim VP9.
