<#
.SYNOPSIS
Build and test Runtime against the verified published Engine package.

.DESCRIPTION
Bootstraps the Engine release pinned by Runtime, passes the returned package
prefix to CMake, builds Runtime as a root project, and runs CTest. No AYTHER
Engine or monorepo checkout is read by this smoke test.
#>
[CmdletBinding()]
param(
    [string]$DependencyDirectory = (Join-Path $PSScriptRoot "../.deps/ayther-engine"),

    [string]$BuildDirectory,

    [ValidateSet("engine", "engine-vpx")]
    [string]$Variant = "engine",

    [string]$ToolchainFile,

    [switch]$ConfigureOnly,

    [switch]$Keep
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Invoke-CheckedCommand {
    param(
        [Parameter(Mandatory)][string]$Command,
        [Parameter(Mandatory)][string[]]$Arguments,
        [Parameter(Mandatory)][string]$FailureMessage
    )

    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw $FailureMessage
    }
}

$runtimeRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$createdBuildDirectory = -not $BuildDirectory
if (-not $BuildDirectory) {
    $BuildDirectory = Join-Path ([IO.Path]::GetTempPath()) `
        "ayther-runtime-oot-$([guid]::NewGuid().ToString('N'))"
}
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)

if (-not $ToolchainFile -and $env:VCPKG_ROOT) {
    $candidate = Join-Path $env:VCPKG_ROOT "scripts/buildsystems/vcpkg.cmake"
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
        $ToolchainFile = $candidate
    }
}

try {
    Write-Host "`n== Bootstrap verified AYTHER Engine package ==" -ForegroundColor Cyan
    $enginePrefix = & (Join-Path $PSScriptRoot "bootstrap_ayther_engine.ps1") `
        -DestinationDirectory $DependencyDirectory `
        -Variant $Variant
    if (-not $enginePrefix -or @($enginePrefix).Count -ne 1) {
        throw "Engine bootstrap did not return exactly one CMake prefix."
    }
    $enginePrefix = [string]$enginePrefix

    Write-Host "`n== Configure Runtime against published Engine ==" -ForegroundColor Cyan
    $cmakePrefixPath = $enginePrefix
    if ($env:CMAKE_PREFIX_PATH) {
        $cmakePrefixPath = "$enginePrefix;$($env:CMAKE_PREFIX_PATH)"
    }
    $configure = @(
        "-S", $runtimeRoot,
        "-B", $BuildDirectory,
        "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_PREFIX_PATH=$cmakePrefixPath"
    )
    if ($ToolchainFile) {
        $configure += "-DCMAKE_TOOLCHAIN_FILE=$([IO.Path]::GetFullPath($ToolchainFile))"
    }
    Invoke-CheckedCommand `
        -Command "cmake" `
        -Arguments $configure `
        -FailureMessage "Runtime could not configure with the published Ayther::engine package."

    if ($ConfigureOnly) {
        Write-Host "`nRuntime standalone configure: OK" -ForegroundColor Green
        return
    }

    Write-Host "`n== Build Runtime ==" -ForegroundColor Cyan
    Invoke-CheckedCommand `
        -Command "cmake" `
        -Arguments @("--build", $BuildDirectory) `
        -FailureMessage "Runtime did not build against Ayther::engine."

    $executable = Join-Path $BuildDirectory "bin/ayther_runtime"
    if ([Runtime.InteropServices.RuntimeInformation]::IsOSPlatform(
            [Runtime.InteropServices.OSPlatform]::Windows)) {
        $executable += ".exe"
    }
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        throw "Runtime build did not produce '$executable'."
    }
    if (-not (Test-Path -LiteralPath (Join-Path $BuildDirectory "bin/shaders/sprite.frag.spv"))) {
        throw "Runtime did not stage shaders from the published Engine package."
    }

    Write-Host "`n== Run Runtime tests ==" -ForegroundColor Cyan
    Invoke-CheckedCommand `
        -Command "ctest" `
        -Arguments @("--test-dir", $BuildDirectory, "--output-on-failure") `
        -FailureMessage "Runtime tests failed against the published Engine package."

    Write-Host "`nRuntime standalone package smoke: OK" -ForegroundColor Green
} finally {
    if (-not $Keep -and $createdBuildDirectory -and
        (Test-Path -LiteralPath $BuildDirectory)) {
        Remove-Item -LiteralPath $BuildDirectory -Recurse -Force
    }
}
