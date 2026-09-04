# MAD-049: inventario del trabajo recuperado

## Punto de recuperación

- SHA base observado: `cf9df6306bcecf8d2c1464926f6f3fcb8e54796d`.
- Rama de respaldo: `feat/mad-049-recovery`.
- Snapshot exacto: `a084e97` (`chore: snapshot uncommitted beta hardening work`).
- Contenido: 33 archivos versionados modificados y 23 archivos nuevos; 56 rutas en total.
- Verificación: SHA-256 de cada ruta contra el working tree original, cero diferencias.

La rama de respaldo conserva el estado original y no es candidata de merge. La
pila atómica siguiente contiene los cambios destinados a integración.

## Código y pruebas por PR

| Rama / PR | Tareas | Código | Pruebas |
| --- | --- | --- | --- |
| `feat/mad-033-034-protocol-errors` | MAD-033, MAD-034 | `src/runtime_error.h`, `src/status_emitter.cpp`, `src/status_emitter.h`, `src/runtime_options.cpp`, `src/runtime_options.h`, hunks de `src/main.cpp` | `tests/protocol_negotiation_test.cmake`, `tests/runtime_error_test.cpp`, `tests/runtime_options_test.cpp`, `tests/status_emitter_json_test.cmake`, `tests/status_emitter_test.cpp`, `tests/cli_error_test.cmake`, `tests/probe_core_test.cmake` |
| `feat/mad-039-041-persistence-capture` | MAD-039, MAD-040, MAD-041 | `src/capture.cpp`, `src/capture.h`, `src/capture_service.cpp`, `src/capture_service.h`, `src/player_config.cpp`, `src/player_config.h`, `src/save_state_store.cpp`, `src/save_state_store.h`, hunks de `src/main.cpp` | `tests/capture_test.cpp`, `tests/player_config_test.cpp`, `tests/save_state_store_test.cpp` |
| `feat/mad-031-032-035-vulkan-lifecycle` | MAD-031, MAD-032, MAD-035 | `src/player_overlay.cpp`, `src/player_overlay.h`, `src/presentation_controller.h`, `src/vulkan_backend/spirv_file.cpp`, `src/vulkan_backend/vk_context.cpp`, `src/vulkan_backend/vk_context.h`, `src/vulkan_backend/vk_postprocess.cpp`, `src/vulkan_backend/vk_postprocess.h`, `src/vulkan_backend/vk_present.cpp`, `src/vulkan_backend/vk_present.h`, `src/vulkan_backend/vk_swapchain.cpp`, `src/vulkan_backend/vk_swapchain.h`, hunks de `src/main.cpp` | `tests/vulkan_context_test.cpp`, `tests/vulkan_presentation_test.cpp`, `tests/vulkan_failure_injection_test.cpp` |
| `feat/mad-037-039-quality` | MAD-037, MAD-038, MAD-039 | `.clang-tidy`, `.github/workflows/ci.yml`, `CMakePresets.json`, `src/runtime_application.cpp`, `src/runtime_application.h`, `src/session_controller.h`, reducción final de `src/main.cpp` | `tests/runtime_fuzz.cpp`, `tests/main_decomposition_test.cmake`, `tests/fuzz_corpus/cli-options`, `tests/fuzz_corpus/metadata-json`, `tests/fuzz_corpus/player-config`, `tests/fuzz_corpus/spirv-loader`, `tests/fuzz_corpus/status-json` |
| `feat/mad-042-windows-packaging` | MAD-042 y `cf9df63` | reglas de instalación/staging e icono Windows | `tests/runtime_package_smoke_test.cmake` |

`CMakeLists.txt` y `tests/CMakeLists.txt` se reparten por hunks: cada PR registra
únicamente sus fuentes, opciones y pruebas. No dependen de archivos sin commit.

## Documentación

| Ruta | Tarea / PR |
| --- | --- |
| `docs/runtime-play-protocol.md` | MAD-033, creada con el contrato y reconciliada por MAD-054. |
| `docs/quality-baseline.md` | MAD-037/MAD-038. |
| `docs/windows-runtime-dependencies.md` | MAD-042. |
| `docs/compatibility-matrix-v1.md` | MAD-036 y PR `feat/mad-036-049-054-docs`. |
| `docs/status.md`, `docs/process-protocol.md`, `CHANGELOG.md`, `README.md` | MAD-054. |

## Artefactos locales

Ninguna de las 56 rutas recuperadas es un artefacto de build o evidencia local.
`.gitignore` excluye `/out/`, `build/`, `cmake-build-*`, `.deps/`,
`graphify-out/`, logs de CMake/CTest, cobertura, perfiles y corpus generados.
`git check-ignore` confirmó `out/`, `out/build`, `.deps/` y `graphify-out/`.

## Reconciliación con `origin/main`

La rama original conserva cinco commits propios respecto del `origin/main`
actual, no ocho. Cuatro entregas históricas ya llegaron a `main` mediante
commits equivalentes (contrato de toolchain, parsing CLI, errores Vulkan y
prueba de arranque); el único commit todavía exclusivo es `cf9df63`, el recurso
de aplicación Windows. El recuento cambió al avanzar las referencias remotas y
no representa pérdida de commits.
