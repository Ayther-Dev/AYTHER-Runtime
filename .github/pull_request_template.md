## Summary

Describe the problem and the outcome of this change.

## Scope and design

- Runtime responsibility affected:
- Engine or launcher contract affected:
- Alternatives considered:

## Verification

- [ ] Configured and built the relevant CMake target.
- [ ] Ran `ctest --preset <preset> --output-on-failure` (`windows-ci`; plus
      `windows-qa` for replay QA, a Tessera-route build for `--provider tessera`).
- [ ] Added or updated tests for changed behavior.
- [ ] Regenerated committed SPIR-V for any GLSL change.
- [ ] Updated affected documentation and protocol examples.
- [ ] Added an entry under `[Unreleased]` in `CHANGELOG.md`.

List platforms, compilers, GPUs/drivers, providers and cores, Engine lock, and
commands exercised:

## Risk and boundaries

- [ ] Preserves `FrameView` and borrowed Vulkan-resource lifetimes.
- [ ] Preserves original-game fallback for optional-feature failures.
- [ ] Works with both emulation providers, or states why one is out of scope.
- [ ] Does not expose protected content, credentials, or personal data.
- [ ] Does not weaken BYOC or third-party licensing boundaries.
- [ ] Documents protocol, persistence, performance, and compatibility impact.

Known limitations and rollback approach:
