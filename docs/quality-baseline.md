# Static analysis, sanitizers, coverage, and fuzz baseline

Baseline version: 1. Date: 2026-09-03.

- Strict warnings: `/W4 /WX /permissive- /EHsc` on MSVC; `-Wall -Wextra -Wpedantic
  -Werror` on Clang/GCC. Third-party headers remain system/imported headers.
- clang-tidy: the repository `.clang-tidy` is enabled by
  `AYTHER_ENABLE_CLANG_TIDY`; selected findings are errors. Baseline accepted
  findings: 0. Suppressions require a local `NOLINT` with a reason.
- Sanitizers: Linux Clang builds use ASan + UBSan with frame pointers. Baseline
  sanitizer findings: 0.
- Coverage: CPU-owned parser/persistence/protocol sources are measured with
  gcovr. Initial line floor: 60%. Lowering it requires a review note in this
  file.
- Fuzzing: `runtime_fuzz` covers CLI tokenization, player config, status JSON,
  capture metadata JSON, and SPIR-V loading. CI uses the checked-in seed corpus,
  `-runs=1000`, `-max_len=4096`, and a 30-second CTest limit.

Any fuzzer crash must be minimized into `tests/fuzz_corpus/` and also converted
to a named deterministic unit regression before the fix is merged.
