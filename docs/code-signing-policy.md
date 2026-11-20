# Code signing policy

Windows releases of AYTHER Runtime are code-signed so that Windows can show who
published them and that they were not modified after the build.

Free code signing provided by [SignPath.io](https://about.signpath.io),
certificate by [SignPath Foundation](https://signpath.org).

## What is signed

- `bin/ayther_runtime.exe` in the Windows release archive
  (`ayther-runtime-<version>-Windows-AMD64-Runtime.zip`).
- Only binaries built from this repository's source are signed. Bundled
  third-party libraries (SDL3, toml++, the Vulkan loader, zstd) are
  distributed as their upstream projects publish them.

Signing happens only in the GitHub Actions
[release workflow](../.github/workflows/release.yml), on a `v*-beta.*` tag,
from the build of that tag. Nothing built on a developer machine is signed.
The SignPath artifact configuration is kept in
[`.signpath/artifact-configurations/windows-package.xml`](../.signpath/artifact-configurations/windows-package.xml).

## Team roles

| Role | Members |
| --- | --- |
| Committers and reviewers | [Members of the Ayther-Dev organization](https://github.com/orgs/Ayther-Dev/people) with write access |
| Approvers | David Lazarte ([@davidlazarte](https://github.com/davidlazarte)) |

Every signing request is approved manually by an approver. All team members
use multi-factor authentication for GitHub and SignPath.

## Privacy policy

This program will not transfer any information to other networked systems
unless specifically requested by the user or the person installing or
operating it.

AYTHER Runtime runs games and content packs from local files only. It has no
telemetry, update check or network access of its own.

## Provider

SignPath Foundation is the signing provider while AYTHER Runtime is in beta.
Changing the provider replaces only the signing steps of the release workflow;
releases signed before the change keep their timestamped signatures.
