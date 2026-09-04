# Runtime–Engine compatibility contract

AYTHER Runtime `0.1.0-prerelease` supports the installed AYTHER Engine package
range `>=0.1.0,<0.2.0`. Reproducible builds use the narrower locked artifact
`v0.1.0-rc.6`, including its published checksum and SLSA provenance.

| Platform | Architecture | Compiler/ABI | C++ runtime |
| --- | --- | --- | --- |
| Windows | x86-64 | MSVC ABI, toolset v145 14.51 or newer within v145; `cl` or `clang-cl` | Dynamic MSVC/UCRT (`/MD`, `/MDd` for Debug) |
| Ubuntu 24.04 | x86-64 | Clang 18.x | libstdc++ |

Configuration rejects an absent version, a package below `0.1.0` or at/above
`0.2.0`, a non-x86-64 artifact, an unsupported operating system, or a compiler
outside the table. The diagnostic reports both the discovered values and the
required range, before Runtime links its main executable.

Runtime includes only installed `ayther/*.h` and `ayther/engine/*.hpp` headers
and links only the exported `Ayther::engine` target. No Engine source-tree,
private header, individual library path, or inferred package layout is part of
the contract.
