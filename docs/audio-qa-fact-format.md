# Audio QA fact format

Reference for the `fact_batch` payload carried by the Runtime QA binary
protocol 1.0 and stored in `.aqf` fragments (see
[audio-qa-evidence.md](audio-qa-evidence.md#facts-and-audio)).

## Batch framing

- The payload starts with a little-endian `uint32` record count.
- Each record is preceded by its little-endian `uint32` length.
- Limits: a record at most 64 KiB; a batch at most 256 KiB and 1024 records.
  They are checked before any allocation.

## Record schemas

| Schema | Encoding | Use |
| --- | --- | --- |
| 1.0 | Full TOML | Historical format with every `Fact` field. |
| 1.1 | Compact TOML | Facts without causes whose five optional fields are `not_applicable` and `compact_default`. |
| 1.2 | Detailed TOML | Typed fields as tables. |
| 1.3 | Compact detailed TOML | The same typed fields as compact arrays. |
| 1.4 | Binary `AQF4` | Current encoding for detailed facts. |

- The writer uses 1.1 for compact facts without fields and 1.4 for facts with
  detailed fields. 1.0–1.3 remain readable so published evidence can be
  reopened.
- An unknown version, an out-of-range value, an inconsistent length or trailing
  bytes are rejected. The reader never infers defaults from another schema.

## `AQF4`

`AQF4` starts with its magic, version 1.4 and explicit lengths. It encodes, in
little-endian: fact identity, producer and sequence, frame, class, causes,
optional identities, reason, shared orders and every typed field. Strings carry
their length and are validated before copying. Decoding rebuilds exactly the
same `Fact` model; it does not alter causality, availability or reasons.

Schema 1.1 gives the reproducible 256-byte-per-record load used by budget B06.
Schema 1.4 reduces the cost of real traces with millions of fields without
lowering limits or dropping data. The outer framing, its hashes and the
emission, reception and durability counters are verified separately.
