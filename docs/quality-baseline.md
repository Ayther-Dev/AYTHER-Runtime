# Static analysis, sanitizer, coverage and fuzz baseline

Baseline version 1, set 2026-09-03. CI enforces it on every pull request
through the `linux-quality` and `linux-sanitizers` presets; see
[Development guide](development.md#continuous-integration).

| Gate | Rule | Accepted findings |
| --- | --- | --- |
| Strict warnings | `/W4 /WX /permissive- /EHsc` on MSVC; `-Wall -Wextra -Wpedantic -Werror` on Clang and GCC (`AYTHER_STRICT_WARNINGS`, on by default). Third-party headers are system or imported headers. | 0 |
| clang-tidy | The repository `.clang-tidy`, enabled by `AYTHER_ENABLE_CLANG_TIDY`. Selected findings are errors. A suppression needs a local `NOLINT` with a reason. | 0 |
| Formatting | `.clang-format`, checked with clang-format 18 on the C++ files a pull request changes (`tools/run_clang_format_changed.sh`). | — |
| Sanitizers | Linux Clang builds with ASan and UBSan and frame pointers (`AYTHER_ENABLE_SANITIZERS`). | 0 |
| Coverage | gcovr line coverage of the CPU-owned parser, persistence and protocol sources (`runtime_options`, `status_emitter`, `player_config`, `capture`, `capture_service`, `save_state_store`, `vulkan_backend/spirv_file`). Floor: 60%. | — |
| Fuzzing | `runtime_fuzz` (`AYTHER_ENABLE_FUZZING`) covers CLI tokenization, player config, status JSON, capture metadata JSON and SPIR-V loading. `runtime_fuzz_smoke` runs the checked-in corpus with `-runs=1000` and `-max_len=4096` within a 30-second CTest limit. | 0 crashes |

Lowering the coverage floor requires a review note in this file.

A fuzzer crash is minimized into `tests/fuzz_corpus/` and converted into a
named deterministic unit regression before the fix is merged.
