# AYTHER Runtime Technical Specification Index

This file preserves the historical `docs/specs.md` entry point while avoiding a
second, drifting copy of the Runtime specification.

## Normative documents

The following documents collectively define the current Runtime contract:

1. [Architecture](architecture.md) defines component boundaries, ownership,
   lifetimes, frame flow, persistence, and failure behavior.
2. [Process and CLI contract](process-protocol.md) defines invocation, status
   events, exit behavior, and launcher integration.
3. [Project status](status.md) records maturity, known gaps, and the conditions
   required before a stable release.
4. [Runtime/Engine dependency audit](runtime-engine-dependency-audit.yaml)
5. [Runtime/Engine compatibility contract](runtime-engine-compatibility.md)
   records the evidence and migration decisions behind the package boundary.
5. [`CMakeLists.txt`](../CMakeLists.txt), [`vcpkg.json`](../vcpkg.json), and the
   tests are the executable source of truth when prose and implementation differ.

## Requirement language

The words **MUST**, **MUST NOT**, **SHOULD**, **SHOULD NOT**, and **MAY** express
requirement strength. They are used in their ordinary software-specification
sense; this project does not currently claim formal RFC conformance.

## Stability

This is a pre-release specification for version `0.1.0`. No interface described
here is stable unless a future release explicitly marks it as such. Changes must
update the relevant document and its tests in the same pull request.
