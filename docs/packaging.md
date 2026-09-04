# Runtime package layout

`cmake --install <build> --prefix <destination>` installs the `Runtime`
component as a relocatable tree:

```text
bin/
  ayther_runtime[.exe]
  <non-system shared dependencies>
  shaders/
share/
  ayther-runtime/ayther-runtime-package.json
  licenses/ayther-runtime/LICENSE
```

The executable locates `bin/shaders` from `SDL_GetBasePath()` and uses `$ORIGIN`
as its installed Linux runtime search path. It does not retain a source/build
directory in its asset lookup. CPack exposes the single `Runtime` component as
ZIP and TGZ archives.

`runtime_package_smoke` consumes only `cmake --install` output, launches from a
separate empty directory, verifies the protocol and installed data, and proves
that removing a packaged shared dependency prevents startup.
