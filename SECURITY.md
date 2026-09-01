# Security Policy

## Supported versions

AYTHER Runtime has no supported production releases yet. Version `0.1.0` and the
default branch are early-development snapshots and do not receive a guaranteed
security-support window or service-level commitment.

| Version | Support status |
| --- | --- |
| Default branch / `0.1.x` snapshots | Best-effort investigation only |
| Tagged stable releases | None published |
| Older snapshots and downstream forks | Not supported by this repository |

Maintainers may still investigate credible reports against the current code and
will document support expectations before the first stable release.

## Report a vulnerability

Do **not** open a public issue, discussion, or pull request for a suspected
vulnerability.

Use the repository's private GitHub vulnerability-reporting or Security Advisory
workflow when it is available. If it is unavailable, contact the repository
maintainers through an established private organization channel and ask for a
secure reporting path. Do not include exploit details in a public message.

Include, when safe and relevant:

- affected commit or version;
- platform, compiler, GPU/driver, core, and launch mode;
- impact and required attacker capabilities;
- minimal reproduction steps or a proof of concept;
- whether untrusted core, ROM, pack, patch, manifest, path, or save-state input
  is involved; and
- suggested mitigation, if known.

Never attach copyrighted game content, private keys, access tokens, personal
data, or third-party binaries that you are not authorized to share.

## What to expect

Maintainers will attempt to acknowledge a report, reproduce it with synthetic or
lawfully shareable inputs, assess affected boundaries, and coordinate a fix and
disclosure when appropriate. Pre-release status means no acknowledgement,
remediation, embargo, or release deadline is guaranteed.

## Scope and trust boundaries

High-priority areas include native core loading, pack and patch parsing, save and
configuration paths, process-event serialization, Vulkan resource lifetimes,
FFI boundaries, and malformed launcher input.

A user-supplied Libretro core is native code running inside the Runtime process.
Runtime's separation from AYTHER Play reduces launcher exposure but is not a
sandbox and does not make an untrusted core safe.

## Disclosure

Allow maintainers reasonable time to investigate and coordinate a fix before
public disclosure. Because the project is pre-release, response and remediation
times are best-effort and no fixed timeline is promised.
