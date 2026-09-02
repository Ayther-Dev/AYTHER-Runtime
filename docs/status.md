# Project Status and Release Gates

## Current maturity

AYTHER Runtime `0.1.0` is an early, pre-release component under active
separation from the AYTHER monorepo. It is suitable for engineering integration
and focused testing. It is **not** presented as production-ready, generally
available, or backward compatible.

There are currently:

- no supported release line;
- no stable Runtime/Engine ABI or package surface;
- no stable launcher protocol version;
- no end-user installer or standalone engine distribution in this repository;
- no declared long-term save-state compatibility policy; and
- no completed distribution-wide third-party license inventory or notice bundle.

## Verified today

The source tree contains focused tests for player configuration, split geometry,
capture metadata, pack-layer assembly, diagnostic decisions, and the real
core-probe process contract. The standalone smoke bootstraps the pinned,
attested Engine release and proves that Runtime can consume its CMake package
without an Engine or monorepo checkout.

A clean vcpkg manifest installation resolves the complete native dependency
closure advertised by `AytherConfig.cmake`; the manifest oracle also preserves
Runtime's direct ImGui and stb dependencies and their required backends.

These checks do not prove broad GPU, driver, operating-system, core, or game
compatibility.

## Known technical gaps

1. `Ayther::engine` exposes a broad first-party surface whose stability is not
   guaranteed. Several Runtime responsibilities still depend on engine backend
   types or raw FFI details.
2. Core probing should move behind a narrow engine API, and normalized input
   should not require Runtime to include the full Libretro header.
3. Vulkan ownership needs a stable public interop structure and render-image
   view with explicit handle, layout, and lifetime rules.
4. Runtime presentation policy and configuration paths should move fully out of
   engine-facing configuration types.
5. `lab_interface.h` is a no-op legacy adapter and is scheduled for removal.
6. Numeric CLI parsing does not consistently detect malformed values or
   overflow. The core-probe JSON escape path does not cover all control
   characters.
7. Capture currently relies on an stb implementation that arrives indirectly
   through the broad engine target. Runtime should own that implementation or
   link a dedicated image writer explicitly.
8. Manual staging of `tomlplusplus` should be verified against the actual link
   closure and removed if the executable no longer imports it.
9. GPU-dependent integration coverage, compatibility matrices, packaging, and
   reproducible release automation are not yet established.

The evidence and proposed ownership for these items are recorded in
[`runtime-engine-dependency-audit.yaml`](runtime-engine-dependency-audit.yaml).

## Stable-release gates

A stable release should not be declared until maintainers have:

- verified MPL-2.0 notices and completed the third-party license inventory for
  every distributed artifact;
- narrowed and versioned the Runtime/Engine package contract;
- versioned the launcher process protocol and defined compatibility behavior;
- completed strict CLI and JSON serialization validation;
- defined supported platforms, compilers, Vulkan requirements, and test cores;
- established clean build, test, security-scanning, and packaging automation;
- documented save/config migration and compatibility policies; and
- completed license review for shipped dependencies and distribution artifacts.

This list is a release-readiness boundary, not a promise of schedule or scope.
