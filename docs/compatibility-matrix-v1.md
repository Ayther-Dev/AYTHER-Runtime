# AYTHER Runtime compatibility matrix v1

This matrix records which platform, GPU, core and provider combinations have
been validated, and when. "Supported" means the row completed a build, the CPU
tests, the package smoke and, for GPU rows, the validation-layer presentation
smoke on the stated date. Rows marked "Untested" or "Planned" are not support
claims. A row stays as recorded until it is rerun; the date says how old the
evidence is.

Minimum graphics API: Vulkan 1.1, with FIFO present mode and a surface format
that can be a color attachment and a transfer destination.

## Operating systems and toolchains

| OS | Compiler | Architecture | Result | Date | Notes |
| --- | --- | --- | --- | --- | --- |
| Windows 11 build 26200 | MSVC v145 14.51 / VS 2026 | x86-64 | Supported | 2026-09-03 | Full build, CPU and package suite and GPU smoke. The CPU suite has passed on every beta since; see [Project status](status.md#latest-validation). |
| Ubuntu 24.04 LTS | Clang 18 + libstdc++ | x86-64 | CI validated; not promoted | 2026-09-03 | Every enabled CPU test passes in CI, including ASan/UBSan and deterministic fuzzing. Linux package and GPU validation are pending. |
| Other Windows or Linux releases | — | — | Untested | — | No support claim. |

## Emulation providers

| Provider | Platform | Result | Date | Notes |
| --- | --- | --- | --- | --- |
| `libretro` | Windows x64, Linux x86-64 | See core coverage | — | Default provider; in every release archive. |
| `tessera` (Mega Drive) | Windows x64 | Validated locally; not promoted | 2026-11-13 | Six `tessera_runtime_*` tests, including GPU presentation and device audio, against `native-v0.1.0-rc.23` and Tessera `v0.2.0-rc.9`. Not in the release archives; no CI run. |

## GPU coverage

| OS / GPU class | Adapter | Driver/API | Result | Date | Notes |
| --- | --- | --- | --- | --- | --- |
| Windows / integrated | Intel Xe-class | — | Untested | — | Not supported. |
| Windows 11 build 26200 / discrete | NVIDIA GeForce RTX 3060 Laptop GPU | Driver 616.224.0; Vulkan 1.4.351 | Supported | 2026-09-03 | Two presents, resize and rebuild, injected allocator failure and double teardown, with validation on and 0 errors. |
| Ubuntu / integrated | Intel Mesa ANV | Vulkan 1.1+ | Untested | — | Headless CI skips the GPU smoke with code 77 when no surface or GPU exists. |
| Ubuntu / discrete | AMD RADV or NVIDIA proprietary | Vulkan 1.1+ | Untested | — | Not supported. |

`vulkan_gpu_smoke` creates a hidden SDL Vulkan window, initializes the device
and swapchain, presents, resizes, rebuilds, presents again, tears down twice,
and fails on any validation error. Its log records the GPU, vendor, driver,
Vulkan API version, queue families and validation status.

## Core coverage (libretro provider)

| Core | License | Result | Date | Notes |
| --- | --- | --- | --- | --- |
| Repository synthetic Libretro core | Test fixture | Supported | 2026-09-03 | Probe and launch contract without ROM content. |
| Repository non-core shared library | Test fixture | Supported negative case | 2026-09-03 | Must produce the stable invalid-core result. |
| SameBoy Libretro | MIT | Planned | — | Use public-domain or homebrew content only. |
| mGBA Libretro | MPL-2.0 | Planned | — | Use public-domain or homebrew content only. |

No real core and GPU combination is supported until a dated validation-layer
run is recorded in this matrix.
