# Windows executable dependency audit

Audit date: 2026-09-03. Artifact: MSVC v145 RelWithDebInfo x86-64.

`dumpbin /dependents ayther_runtime.exe` reports the following non-system
runtime libraries:

| Import | Required | Staging owner |
| --- | --- | --- |
| `SDL3.dll` | Yes | Direct `SDL3::SDL3` target. |
| `tomlplusplus-3.dll` | Yes | Direct `tomlplusplus::tomlplusplus` target. |
| `zstd.dll` | Yes | Engine package transitive import. |
| `vulkan-1.dll` | Yes | Vulkan loader imported before `main`; staged for clean machines. |

The former `file(GLOB tomlplusplus*.dll)` workaround was removed. Runtime now
declares tomlplusplus directly, so CMake's `TARGET_RUNTIME_DLLS` stages the
exact target artifact and follows soname/configuration changes.

MSVC/UCRT and Windows libraries (`KERNEL32`, `USER32`, `SHELL32`, `USERENV`,
`WS2_32`, `IMM32`, `ntdll`, `bcryptprimitives`, and API-set contracts) are OS or
Visual C++ Redistributable prerequisites and are not copied by this project.

The `runtime_package_smoke` CTest runs `cmake --install` into an empty prefix,
verifies the tomlplusplus artifact and shaders exist in the installed layout,
and launches that installed binary. The intentional CLI exit 64
proves all loader-time imports resolved and control reached `main()`; CI runs
this after every package build.
