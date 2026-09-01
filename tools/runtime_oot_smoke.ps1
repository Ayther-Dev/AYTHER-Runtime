# ---------------------------------------------------------------------------
# runtime_oot_smoke.ps1 — el runtime FUERA DEL ÁRBOL, contra el paquete `Ayther`
# instalado (ADR-004 E8.1 / E6.1).
#
# El runtime es el consumidor de REFERENCIA del motor: usa la superficie de
# primera parte (componente `engine`), no sólo el contrato de ADR-003. Adentro
# del monorepo su CMakeLists consume el paquete del árbol; acá se prueba que no
# hace trampa: se instala el paquete en un prefijo limpio y se configura y
# compila `runtime/` como proyecto RAÍZ contra ese prefijo, sin el monorepo en
# el include path. Si esto pasa, el día del corte cambia el prefijo y nada más —
# que es exactamente lo que E8.1 promete. Lo que este smoke no prueba, no se
# cumple.
#
# Vive en runtime/tools y no en el smoke del SDK: el SDK no tiene por qué saber
# que el runtime existe (frontera motor/sdk → consumidores, E8.3). Lo que sí
# recibe es de dónde instalar el paquete: un build dir del motor.
#
# Uso:  pwsh runtime/tools/runtime_oot_smoke.ps1 [-BuildDir build] [-Keep]
# ---------------------------------------------------------------------------
param(
    [string]$BuildDir = "build",
    [switch]$Keep
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path "$PSScriptRoot/../..").Path

$prefix = Join-Path ([System.IO.Path]::GetTempPath()) "ayther-runtime-oot-prefix"
$bld    = Join-Path ([System.IO.Path]::GetTempPath()) "ayther-runtime-oot-build"
foreach ($d in @($prefix, $bld)) {
    if (Test-Path $d) { Remove-Item -Recurse -Force $d }
}

function Paso($n) { Write-Host "`n== $n ==" -ForegroundColor Cyan }

# -- 1. Instalar el paquete con la superficie de primera parte ---------------
Paso "instalar el paquete Ayther engine en un prefijo limpio"
cmake --install $BuildDir --prefix $prefix | Out-Null
if ($LASTEXITCODE -ne 0) { throw "cmake --install del paquete engine falló" }
foreach ($p in @("lib/cmake/Ayther/AytherConfig.cmake",
                 "include/ayther/ayther_session.h",
                 "include/ayther/ayther_renderer.h",
                 "include/ayther/vulkan_backend/vk_context.h",
                 "share/ayther/shaders/sprite.frag.spv")) {
    if (-not (Test-Path (Join-Path $prefix $p))) { throw "falta en el paquete: $p" }
}
Write-Host "  [ OK ] paquete instalado con el componente engine"

# -- 2. Configurar y compilar el runtime como proyecto raíz ------------------
#
# Las dependencias (SDL3, Vulkan, imgui…) salen del vcpkg del build, igual que
# en el smoke del SDK: lo que se prueba es el paquete, no el gestor de paquetes.
# `runtime/vcpkg.json` es lo que un clon del repo separado declararía.
Paso "configurar y compilar runtime/ fuera del árbol"
$deps = Join-Path $repo "$BuildDir/vcpkg_installed/x64-windows"
$cxx  = "C:/Program Files/LLVM/bin/clang-cl.exe"
$args = @("-S", (Join-Path $repo "runtime"), "-B", $bld, "-G", "Ninja",
          "-DCMAKE_PREFIX_PATH=$prefix;$deps", "-DCMAKE_BUILD_TYPE=Release")
if (Test-Path $cxx) { $args += "-DCMAKE_CXX_COMPILER=$cxx" }
cmake @args | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "el runtime no configuró fuera del árbol (find_package(Ayther 0.1.0 CONFIG COMPONENTS engine) falló)"
}
cmake --build $bld | Out-Null
if ($LASTEXITCODE -ne 0) { throw "el runtime no compiló o no enlazó contra Ayther::engine" }

$exe = Join-Path $bld "bin/ayther_runtime.exe"
if (-not (Test-Path $exe)) { $exe = Join-Path $bld "bin/ayther_runtime" }
# Que ninja diga «Linking» no garantiza el ejecutable: se comprueba el archivo.
if (-not (Test-Path $exe)) { throw "el runtime no dejó ejecutable" }

# Los shaders del motor tienen que haber llegado desde el PAQUETE
# (Ayther_SHADER_DIR), no desde el repo: un runtime que compila y no dibuja es
# el peor de los dos fallos.
if (-not (Test-Path (Join-Path $bld "bin/shaders/sprite.frag.spv"))) {
    throw "el runtime no escenificó los shaders del motor desde Ayther_SHADER_DIR"
}
Write-Host "  [ OK ] ayther_runtime compila y enlaza contra el paquete instalado"

# -- 3. Sus oráculos, contra el paquete ---------------------------------------
Paso "ctest de runtime/tests fuera del árbol"
ctest --test-dir $bld --output-on-failure | Out-Null
if ($LASTEXITCODE -ne 0) { throw "los tests del runtime fallaron fuera del árbol" }
Write-Host "  [ OK ] runtime/tests en verde contra el paquete"

if (-not $Keep) {
    Remove-Item -Recurse -Force $bld, $prefix
}
Write-Host "`nRuntime out-of-tree: OK" -ForegroundColor Green
