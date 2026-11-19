# Windows executable dependencies

Checked: 2026-11-19, by listing the imports of the installed `windows-ci`
RelWithDebInfo tree (Runtime `0.1.0-beta.18` plus the unreleased changes, MSVC
v145, x86-64, Engine `v0.1.0-rc.28`). The set is unchanged since the first audit on 2026-09-03.

Non-system libraries that the installed tree must contain:

| Import | Staged because |
| --- | --- |
| `SDL3.dll` | Direct `SDL3::SDL3` target. |
| `tomlplusplus-3.dll` | Direct `tomlplusplus::tomlplusplus` target. |
| `zstd.dll` | Transitive import of the Engine package. |
| `vulkan-1.dll` | Vulkan loader, imported before `main`; staged so a clean machine starts. |

CMake stages these from `TARGET_RUNTIME_DLLS`, so the exact target artifacts
are copied even when their names or configurations change.

The MSVC runtime (`MSVCP140`, `VCRUNTIME140`, `VCRUNTIME140_1`), the UCRT
API sets (`api-ms-win-crt-*`, `api-ms-win-core-*`) and Windows system libraries
(`KERNEL32`, `USER32`, `SHELL32`, `USERENV`, `WS2_32`, `IMM32`, `ntdll`,
`bcryptprimitives`) are not copied. They come from Windows and the Visual C++
Redistributable, which must be installed on the target machine.

The `runtime_package_smoke` test checks this set on every CTest run; see
[Runtime package layout](packaging.md).
