# AYTHER Runtime compatibility matrix v1

Last updated: 2026-09-03. “Supported” means the exact row completed build, CPU
tests, package smoke, and (for GPU rows) the validation-layer presentation
smoke. Planned or untested rows are not supported claims.

Minimum graphics API: Vulkan 1.1. FIFO present mode and a surface format usable
as color attachment plus transfer destination are required.

## Operating systems and toolchains

| OS | Compiler | Architecture | Result | Date | Notes |
| --- | --- | --- | --- | --- | --- |
| Windows 11 build 26200 | MSVC v145 14.51 / VS 2026 | x86-64 | Supported | 2026-09-03 | Full build, CPU/package suite and GPU smoke passed. |
| Ubuntu 24.04 LTS | Clang 18 + libstdc++ | x86-64 | CI validated; not promoted | 2026-09-03 | Clean build and every enabled CPU test passed with Clang 18.1.3, including ASan/UBSan and deterministic fuzzing; Linux package/GPU validation remains pending. |
| Other Windows/Linux releases | — | — | Untested | 2026-09-03 | No support claim. |

## GPU coverage

| OS / GPU class | Candidate adapter | Driver/API | Result | Date | Known failures |
| --- | --- | --- | --- | --- | --- |
| Windows / integrated | Intel Xe-class | To be recorded by `vulkan_gpu_smoke` | Untested | 2026-09-03 | None recorded; not yet supported. |
| Windows 11 build 26200 / discrete | NVIDIA GeForce RTX 3060 Laptop GPU | Driver 616.224.0; Vulkan 1.4.351 | Supported | 2026-09-03 | Two presents, resize/rebuild, injected allocator failure and double teardown passed with validation on and 0 errors. |
| Ubuntu / integrated | Intel Mesa ANV | Vulkan 1.1+ | Untested | 2026-09-03 | Headless CI skips with code 77 when no surface/GPU exists. |
| Ubuntu / discrete | AMD RADV or NVIDIA proprietary | Vulkan 1.1+ | Untested | 2026-09-03 | None recorded; not yet supported. |

`vulkan_gpu_smoke` creates a real hidden SDL Vulkan window, initializes the
device and swapchain, presents, resizes, rebuilds, presents again, tears down
twice, and fails on any validation error. Its logs record GPU, vendor, driver,
Vulkan API, queue families, and validation status.

## Core coverage

| Core | License/use | Result | Date | Notes |
| --- | --- | --- | --- | --- |
| Repository synthetic Libretro core | Test fixture | Supported | 2026-09-03 | Covers probe and launch contract without redistributable ROM content. |
| Repository non-core shared library | Test fixture | Supported negative case | 2026-09-03 | Must produce stable invalid-core behavior. |
| SameBoy Libretro | MIT | Planned / untested | 2026-09-03 | Use public-domain/homebrew content only. |
| mGBA Libretro | MPL-2.0 | Planned / untested | 2026-09-03 | Use public-domain/homebrew content only. |

Known limitation: no real core/GPU combination is promoted to supported until a
dated validation-layer run is attached to this table.
