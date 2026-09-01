# Support

AYTHER Runtime is an early-development component for maintainers and integrators.
There is no production support commitment, response-time guarantee, or stable
compatibility matrix.

## Where to ask

- Use a GitHub issue with the appropriate form for a reproducible defect or a
  scoped enhancement proposal, when Issues are enabled.
- Use the parent AYTHER project's established support channel for end-user
  launcher, account, library, download, or cloud-sync questions.
- Follow [SECURITY.md](SECURITY.md) for vulnerabilities; never report them in a
  public support thread.

## Information to include

Provide the commit, operating system, compiler, CMake generator, AYTHER package
revision, vcpkg baseline, GPU and driver, core identity, exact command with
sensitive paths redacted, relevant logs, expected behavior, and observed
behavior.

Do not upload ROMs, cores, packs, save states, screenshots containing protected
content, credentials, or personal data. A minimal synthetic reproduction is
preferred.

Build failures caused by a missing `Ayther::frontend` package are expected in a
standalone checkout until an installed AYTHER prefix is supplied. See the
[development guide](docs/development.md) before filing an issue.
