# Project Status and Release Gates

## Current maturity

AYTHER Runtime `0.1.0-beta.2` is an internal, pre-release product under active
separation from the AYTHER monorepo. It is suitable for engineering integration
and focused testing. It is **not** presented as production-ready, generally
available, or backward compatible.

The Runtime–Play process protocol v1 is stable. That is a deliberately narrow
wire-compatibility promise for status records, reason identifiers, negotiation,
and exit codes; it does not stabilize the Runtime product, Engine ABI, package,
configuration, or save-state formats.

There are currently:

- no supported stable release line; `v0.1.0-beta.2` is for internal evaluation;
- no stable Runtime/Engine ABI or package surface;
- one stable launcher protocol version (v1), with future breaking changes
  requiring a new protocol version;
- no end-user installer or standalone engine distribution in this repository;
- no declared long-term save-state compatibility policy; and
- no completed distribution-wide third-party license inventory or notice bundle.

## Verified today

The source tree contains focused tests for Runtime path resolution, output
profiles, player configuration, split geometry, capture metadata, pack-layer
assembly, typed pack/watcher APIs, bounded game telemetry, diagnostic decisions,
and the real core-probe process contract. The
standalone smoke bootstraps the pinned, attested Engine release and proves its
CMake configuration and public-header contract without an Engine or monorepo
checkout. The pinned `v0.1.0-rc.6` package configures successfully and Runtime
sources compile and link against its installed public surface.

A clean vcpkg manifest installation resolves the complete native dependency
closure advertised by `AytherConfig.cmake`; the manifest oracle also preserves
Runtime's direct ImGui and stb dependencies and their required backends.

On 2026-09-03, pull request #2 was merged into `main` as
`d924f4b7b4fe67a32970d7ae2e8088ce87cb0c19`. From that clean Windows x64
checkout (Windows 25H2, build 26200.9278), the standalone smoke bootstrapped
and verified the pinned Engine `v0.1.0-rc.6` archive, including its locked and
published SHA-256 values and SLSA provenance. CMake 4.3.3 and Ninja configured
and built all 58 steps with MSVC 19.51.36256.0 from the v145 14.51.36231
toolset. PowerShell 7.5.2 enabled the complete test inventory, and CTest passed
19/19 tests with zero failures or tests omitted at the CTest level. The full
local smoke log is retained at
`build-mad001-d924f4b/mad-001-smoke.log` (995,642 bytes; SHA-256
`014338932bb1f8c1cea012598f522d5adb538348286fd307f5302d77950a7ab0`).

These checks do not prove broad GPU, driver, operating-system, core, or game
compatibility.

## Known technical gaps

1. `Ayther::engine` remains provisional, although Runtime now consumes its
   typed pack, input, renderer, core-probe, and Vulkan-interoperability
   contracts exclusively through installed public headers.
2. Windows is verified with MSVC v145 14.51; older toolsets are intentionally
   rejected during configure. Ubuntu 24.04/Clang 18 remains gated by the remote
   quality and sanitizer jobs for each integration SHA.
3. GPU coverage currently represents one discrete Windows adapter. Integrated
   GPUs and Linux display stacks remain unverified and are not support claims.
4. Packaging smoke coverage exists, but an end-user installer, signed release
   automation, and a complete distribution license/notice bundle do not.

The evidence and proposed ownership for these items are recorded in
[`runtime-engine-dependency-audit.yaml`](runtime-engine-dependency-audit.yaml).

## Stable-release gates

A stable release should not be declared until maintainers have:

- verified MPL-2.0 notices and completed the third-party license inventory for
  every distributed artifact;
- narrowed and versioned the Runtime/Engine package contract;
- preserved the stable v1 launcher protocol and versioned any future breaking
  wire changes;
- defined supported platforms, compilers, Vulkan requirements, and test cores;
- established clean build, test, security-scanning, and packaging automation;
- documented save/config migration and compatibility policies; and
- completed license review for shipped dependencies and distribution artifacts.

This list is a release-readiness boundary, not a promise of schedule or scope.
