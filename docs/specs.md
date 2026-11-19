# AYTHER Runtime specification index

This index lists the documents that define the Runtime contract. Each topic has
one source; other documents link to it.

## Normative documents

| Topic | Document |
| --- | --- |
| Component boundaries, ownership, providers, lifetimes, frame flow, persistence, failure behavior | [Architecture](architecture.md) |
| Invocation, options, status events, exit codes | [Process and CLI contract](process-protocol.md) |
| Runtime–Play protocol v1 (the stable promise): negotiation, exit codes, error taxonomy | [Runtime–Play protocol](runtime-play-protocol.md) |
| ROM and pack reservations | [File reservations](specs/runtime-reservations.md) |
| Input map schema | [Input map](input-map.md) |
| Pack trust registry | [Trust registry](trust-registry.md) |
| Engine package range, locks and the Tessera route | [Runtime–Engine compatibility](runtime-engine-compatibility.md) |
| Engine dependency evidence and ownership decisions | [Runtime/Engine dependency audit](runtime-engine-dependency-audit.yaml) |
| Installed layout and release archives | [Runtime package layout](packaging.md) |
| Replay QA evidence and fact formats | [Audio QA evidence](audio-qa-evidence.md), [Audio QA fact format](audio-qa-fact-format.md) |

[`CMakeLists.txt`](../CMakeLists.txt), [`vcpkg.json`](../vcpkg.json) and the
tests are the executable source of truth when prose and implementation differ.

## Requirement language

**MUST**, **MUST NOT**, **SHOULD**, **SHOULD NOT** and **MAY** express
requirement strength in their ordinary software-specification sense. The
project does not claim formal RFC 2119 conformance.

## Stability

These documents describe the `0.1.0` prerelease. Only the Runtime–Play
protocol v1 is stable; everything else may change between betas. A change to
behavior updates the relevant document and its tests in the same pull request.
