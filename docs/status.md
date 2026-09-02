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

The source tree contains focused tests for Runtime path resolution, output
profiles, player configuration, split geometry, capture metadata, pack-layer
assembly, typed pack/watcher APIs, bounded game telemetry, diagnostic decisions,
and the real core-probe process contract. The
standalone smoke bootstraps the pinned, attested Engine release and proves that
Runtime can consume its CMake package
without an Engine or monorepo checkout.

A clean vcpkg manifest installation resolves the complete native dependency
closure advertised by `AytherConfig.cmake`; the manifest oracle also preserves
Runtime's direct ImGui and stb dependencies and their required backends.

These checks do not prove broad GPU, driver, operating-system, core, or game
compatibility.

## Known technical gaps

1. `Ayther::engine` remains provisional, although Runtime now consumes its
   typed pack, input, renderer, core-probe, and Vulkan-interoperability
   contracts exclusively through installed public headers.
2. The pinned `v0.1.0-rc.4` Engine artifact predates the public input and
   renderer headers now consumed by Runtime. The dependency lock must advance
   when a matching Engine release is published; the current Engine package
   contract is already build-tested locally.
3. Numeric CLI parsing does not consistently detect malformed values or
   overflow.
4. Manual staging of `tomlplusplus` should be verified against the actual link
   closure and removed if the executable no longer imports it.
5. GPU-dependent integration coverage, compatibility matrices, packaging, and
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
