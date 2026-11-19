# Support

AYTHER Runtime is a prerelease component for maintainers and integrators.
There is no production support commitment or response-time guarantee. What has
been validated is recorded in the [compatibility matrix](docs/compatibility-matrix-v1.md).

## Where to ask

- Open a GitHub issue with the bug-report or feature-request form for a
  reproducible defect or a scoped enhancement.
- Ask questions about the launcher, the game library or downloads in the
  AYTHER Play CE project.
- Follow [SECURITY.md](SECURITY.md) for vulnerabilities; never report them in a
  public support thread.

## Information to include

Provide the Runtime version or commit (the `[main]` version line Runtime prints
at startup), the Engine lock, the operating system, compiler and preset, the
GPU and driver, the provider (`libretro` with the core identity, or
`tessera`), the exact command with sensitive paths redacted, the relevant logs
and `AYTHER_STATUS` lines, and the expected and observed behavior.

Do not upload ROMs, cores, packs, save states, screenshots containing protected
content, credentials, or personal data. A minimal synthetic reproduction is
preferred.

A configure failure about a missing `Ayther::engine` package means the Engine
package was not bootstrapped or `AYTHER_ENGINE_PREFIX` is not set. See the
[development guide](docs/development.md) before filing an issue.
