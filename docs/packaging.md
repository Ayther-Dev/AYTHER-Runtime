# Runtime package layout

`cmake --install <build> --prefix <destination>` installs a relocatable tree.
The release archives are produced from it with CPack.

```text
bin/
  ayther_runtime[.exe]
  <non-system shared dependencies>
  fonts/
  shaders/
  ayther_audio_qa[.exe]        qa component only
  ayther_replay_qa[.exe]       qa component only
share/
  ayther-runtime/ayther-runtime-package.json
  licenses/ayther-runtime/     LICENSE (MPL-2.0) and NOTICE.md (project notice)
  licenses/ayther-engine/      Engine license and third-party notices
  licenses/tessera/            Windows: Tessera licenses, notices, source availability
```

| Component | Contents | Release archive |
| --- | --- | --- |
| `Runtime` | Runtime executable, shared dependencies, fonts, Engine and Runtime shaders, package metadata, Runtime/Engine/Tessera licenses | `ayther-runtime-<version>-Windows-AMD64-Runtime.zip`, `ayther-runtime-<version>-Linux-x86_64-Runtime.tar.gz` |
| `qa` | The replay QA coordinator `ayther_audio_qa` and launcher `ayther_replay_qa` | Part of the QA Runtime archive `ayther-runtime-qa-<version>-Windows-AMD64.zip`, built against the QA Engine package |

Each archive is published with a `SHA256SUMS-*.txt` file.

`share/ayther-runtime/ayther-runtime-package.json` records the layout version,
the Runtime version, the protocol version, the Engine release recorded at build
time (`AYTHER_ENGINE_LOCKED_RELEASE`) and the supported Engine range.

The executable finds `bin/shaders` and `bin/fonts` from `SDL_GetBasePath()`,
not from a source or build directory. On Linux it uses `$ORIGIN` as its runtime
search path. Windows imports are listed in
[Windows executable dependencies](windows-runtime-dependencies.md).

`runtime_package_smoke` consumes only the `cmake --install` output, launches it
from an empty working directory, and checks the protocol record and the
installed data. A second copy with one packaged shared library removed must
fail before protocol startup on Windows; on Linux the test also accepts the
case where the platform Vulkan loader provides the missing library.
