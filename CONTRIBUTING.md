# Contributing to AYTHER Runtime

Thank you for helping improve AYTHER Runtime. The project is in early development,
so changes must preserve its process, ownership, content, and legal boundaries.

## Current contribution policy

AYTHER Runtime is licensed under the [Mozilla Public License 2.0](LICENSE).
Unless explicitly agreed otherwise in writing, contributions accepted into this
repository are made available under MPL-2.0 and become Covered Software as
defined by that license.

Do not submit code, assets, ROMs, cores, packs, patches, or other material unless
you created it or have sufficient rights to contribute it under MPL-2.0. A pull
request does not grant rights to third-party content, AYTHER trademarks, or
materials governed by separate terms. No contributor license agreement is
required unless maintainers state otherwise for a particular contribution.

## Before opening an issue

- Search existing issues and the [project status](docs/status.md).
- Confirm the behavior against the current default branch.
- Remove game content, personal paths, tokens, account data, and other sensitive
  information from logs and captures.
- Use the security process in [SECURITY.md](SECURITY.md) for vulnerabilities.
- Use [SUPPORT.md](SUPPORT.md) for setup questions that are not defects.

## Development workflow

1. Read the [architecture](docs/architecture.md), [development guide](docs/development.md),
   and [process contract](docs/process-protocol.md).
2. Keep each change focused on one behavior or architectural decision.
3. Add or update tests at the lowest practical layer. Prefer pure policy and
   geometry tests where a GPU is not essential.
4. Build with CMake and run `ctest --test-dir build --output-on-failure`.
5. Update all affected documentation, protocol examples, and status notes in the
   same change.
6. Complete the pull request template with evidence, risks, and rollback notes.

## Engineering requirements

- Use C++20 and the existing CMake target model.
- Consume engine functionality through `find_package(Ayther ...)` and imported
  targets; do not add relative includes into engine source or private directories.
- Preserve `FrameView` borrowing and invalidation rules.
- Keep Runtime-owned Vulkan resources separate from engine-owned renderer
  resources. Document every cross-boundary borrowed handle.
- Keep launcher events to complete, one-line `AYTHER_STATUS <json>` records.
- Preserve original gameplay when optional pack or post-process features fail.
- Do not weaken BYOC, content immutability, or third-party licensing boundaries.
- Preserve license notices and identify files or dependencies governed by
  different terms.
- Regenerate and commit SPIR-V whenever its GLSL source changes.
- Avoid unrelated formatting or cleanup in behavior changes.

## Commit and pull request quality

Use a short imperative summary and explain the reason for the change in the
body. A pull request should state:

- the user-visible or architectural problem;
- the chosen solution and alternatives considered;
- tests and environments exercised;
- protocol, persistence, performance, and legal/content impact; and
- known limitations or follow-up work.

Maintainers may require changes to preserve early-stage compatibility or reject
a proposal that expands scope beyond Runtime's single-session role.
