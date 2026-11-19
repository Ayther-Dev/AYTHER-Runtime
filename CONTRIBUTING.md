# Contributing to AYTHER Runtime

Thank you for helping improve AYTHER Runtime. The project is a prerelease, so
changes must preserve its process, ownership, content and legal boundaries.

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
- Confirm the behavior against the current `main` branch and say which
  provider (`libretro` or `tessera`) and Engine lock you used.
- Remove game content, personal paths, tokens, account data, and other sensitive
  information from logs and captures.
- Use the security process in [SECURITY.md](SECURITY.md) for vulnerabilities.
- Use [SUPPORT.md](SUPPORT.md) for setup questions that are not defects.

## Development workflow

1. Read the [architecture](docs/architecture.md), [development guide](docs/development.md)
   and [process contract](docs/process-protocol.md).
2. Before starting, check open and recent pull requests so work is not
   duplicated. Branch from an up-to-date `origin/main`, one branch per topic.
3. Keep each change focused on one behavior or architectural decision.
4. Add or update tests at the lowest practical layer. Prefer pure policy and
   geometry tests where a GPU is not essential.
5. Build and test with the presets, for example
   `cmake --preset windows-ci`, `cmake --build --preset windows-ci` and
   `ctest --preset windows-ci --output-on-failure`. Changes to replay QA also
   need `windows-qa`; changes to the Tessera route need a Tessera-route build
   ([how](docs/runtime-engine-compatibility.md#tessera-route-windows-x64)).
6. Update the affected reference documents in the same change and add an entry
   under `[Unreleased]` in [CHANGELOG.md](CHANGELOG.md).
7. Complete the pull request template with evidence, risks and rollback notes.

## Engineering requirements

- Use C++20 and the existing CMake target model. Format changed C++ files with
  clang-format 18 (`.clang-format`); CI checks whole changed files.
- Consume engine functionality through `find_package(Ayther ...)` and imported
  targets; do not add relative includes into engine source or private directories.
- Preserve `FrameView` borrowing and invalidation rules.
- Keep Runtime-owned Vulkan resources separate from engine-owned renderer
  resources. Document every cross-boundary borrowed handle.
- Keep launcher events to complete, one-line `AYTHER_STATUS <json>` records.
- Preserve original gameplay when optional features fail (a convention-found
  pack, post-processing). An explicitly requested `--pack` stays mandatory.
- Keep both emulation providers working; a session feature must not assume a
  Libretro core is loaded.
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
