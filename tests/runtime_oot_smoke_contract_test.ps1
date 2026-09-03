[CmdletBinding()]
param(
    [string]$SmokeScript = (Join-Path $PSScriptRoot "../tools/runtime_oot_smoke.ps1")
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Assert-Condition {
    param(
        [Parameter(Mandatory)][bool]$Condition,
        [Parameter(Mandatory)][string]$Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

$resolvedScript = (Resolve-Path -LiteralPath $SmokeScript).Path
$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    $resolvedScript,
    [ref]$tokens,
    [ref]$parseErrors)
Assert-Condition ($parseErrors.Count -eq 0) "Smoke script has PowerShell parse errors."

$parameters = @($ast.ParamBlock.Parameters | ForEach-Object {
    $_.Name.VariablePath.UserPath
})
Assert-Condition ($parameters -contains "AytherPrefix") `
    "Smoke script must accept -AytherPrefix."
Assert-Condition ($parameters -contains "EngineArchive") `
    "Smoke script must accept -EngineArchive."

$source = Get-Content -LiteralPath $resolvedScript -Raw
Assert-Condition ($source -notmatch '\$repo\s*[/\\]\s*runtime') `
    "Smoke script must not derive Runtime from the legacy monorepo path."
Assert-Condition ($source -match '\$PSScriptRoot') `
    "Smoke script must derive paths from `$PSScriptRoot."

$testRoot = Join-Path ([IO.Path]::GetTempPath()) `
    "ayther-runtime-smoke-contract-$([guid]::NewGuid().ToString('N'))"
$prefix = Join-Path $testRoot "engine-prefix"
$configDirectory = Join-Path $prefix "lib/cmake/Ayther"
$buildDirectory = Join-Path $testRoot "build"
[IO.Directory]::CreateDirectory($configDirectory) | Out-Null
Set-Content -LiteralPath (Join-Path $configDirectory "AytherConfig.cmake") `
    -Value "# contract-test placeholder" -Encoding utf8NoBOM

$hadVcpkgRoot = Test-Path Env:VCPKG_ROOT
$previousVcpkgRoot = $env:VCPKG_ROOT
$hadExpectedArchive = Test-Path Env:RUNTIME_OOT_EXPECTED_ARCHIVE
$previousExpectedArchive = $env:RUNTIME_OOT_EXPECTED_ARCHIVE
$hadEnginePrefix = Test-Path Env:RUNTIME_OOT_ENGINE_PREFIX
$previousEnginePrefix = $env:RUNTIME_OOT_ENGINE_PREFIX
$global:RuntimeOotSmokeCmakeCalls = [System.Collections.Generic.List[object]]::new()
function global:cmake {
    $global:RuntimeOotSmokeCmakeCalls.Add([string[]]$args)
    $global:LASTEXITCODE = 0
}

try {
    Remove-Item Env:VCPKG_ROOT -ErrorAction SilentlyContinue
    & $resolvedScript `
        -AytherPrefix $prefix `
        -BuildDirectory $buildDirectory `
        -ConfigureOnly

    Assert-Condition ($global:RuntimeOotSmokeCmakeCalls.Count -eq 1) `
        "Configure-only prefix mode must invoke CMake exactly once."
    $arguments = [string[]]$global:RuntimeOotSmokeCmakeCalls[0]
    $sourceIndex = [Array]::IndexOf($arguments, "-S")
    Assert-Condition ($sourceIndex -ge 0 -and $sourceIndex + 1 -lt $arguments.Count) `
        "CMake configure arguments must contain -S <Runtime root>."

    $expectedRuntimeRoot = (Resolve-Path -LiteralPath (
        Join-Path (Split-Path -Parent $resolvedScript) "..")).Path
    Assert-Condition ($arguments[$sourceIndex + 1] -ceq $expectedRuntimeRoot) `
        "CMake -S must point to the independent Runtime clone root."
    Assert-Condition ($arguments -contains "-DCMAKE_PREFIX_PATH=$prefix") `
        "Prefix mode must pass the supplied Engine prefix to CMake."
    Assert-Condition ($arguments -contains "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF") `
        "Standalone configure must disable the user CMake package registry."
    Assert-Condition ($arguments -contains "-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF") `
        "Standalone configure must disable the system CMake package registry."

    $standaloneRoot = Join-Path $testRoot "standalone-runtime"
    $standaloneTools = Join-Path $standaloneRoot "tools"
    $archive = Join-Path $testRoot "engine.zip"
    [IO.Directory]::CreateDirectory($standaloneTools) | Out-Null
    Set-Content -LiteralPath $archive -Value "archive placeholder" -Encoding utf8NoBOM
    Copy-Item -LiteralPath $resolvedScript `
        -Destination (Join-Path $standaloneTools "runtime_oot_smoke.ps1")
    Set-Content -LiteralPath (Join-Path $standaloneTools "bootstrap_ayther_engine.ps1") `
        -Encoding utf8NoBOM -Value @'
[CmdletBinding()]
param(
    [string]$DestinationDirectory,
    [string]$Variant,
    [string]$ArchivePath
)
if ((Resolve-Path -LiteralPath $ArchivePath).Path -cne $env:RUNTIME_OOT_EXPECTED_ARCHIVE) {
    throw "Smoke script did not forward the supplied archive."
}
Write-Output $env:RUNTIME_OOT_ENGINE_PREFIX
'@

    $env:RUNTIME_OOT_EXPECTED_ARCHIVE = (Resolve-Path -LiteralPath $archive).Path
    $env:RUNTIME_OOT_ENGINE_PREFIX = $prefix
    $global:RuntimeOotSmokeCmakeCalls.Clear()
    & (Join-Path $standaloneTools "runtime_oot_smoke.ps1") `
        -EngineArchive $archive `
        -BuildDirectory (Join-Path $testRoot "archive-build") `
        -ConfigureOnly

    Assert-Condition ($global:RuntimeOotSmokeCmakeCalls.Count -eq 1) `
        "Configure-only archive mode must invoke CMake exactly once."
    $archiveArguments = [string[]]$global:RuntimeOotSmokeCmakeCalls[0]
    $archiveSourceIndex = [Array]::IndexOf($archiveArguments, "-S")
    Assert-Condition ($archiveSourceIndex -ge 0) `
        "Archive mode CMake arguments must contain -S <Runtime root>."
    Assert-Condition ($archiveArguments[$archiveSourceIndex + 1] -ceq $standaloneRoot) `
        "Archive mode must derive the Runtime root from the copied script."

    $rejected = $false
    try {
        & $resolvedScript `
            -AytherPrefix $prefix `
            -EngineArchive (Join-Path $testRoot "engine.zip") `
            -BuildDirectory $buildDirectory `
            -ConfigureOnly
    } catch {
        $rejected = $_.Exception.Message -match 'cannot be used together'
    }
    Assert-Condition $rejected `
        "-AytherPrefix and -EngineArchive must be mutually exclusive."
} finally {
    Remove-Item Function:\global:cmake -ErrorAction SilentlyContinue
    Remove-Variable RuntimeOotSmokeCmakeCalls -Scope Global -ErrorAction SilentlyContinue
    if ($hadVcpkgRoot) {
        $env:VCPKG_ROOT = $previousVcpkgRoot
    } else {
        Remove-Item Env:VCPKG_ROOT -ErrorAction SilentlyContinue
    }
    if ($hadExpectedArchive) {
        $env:RUNTIME_OOT_EXPECTED_ARCHIVE = $previousExpectedArchive
    } else {
        Remove-Item Env:RUNTIME_OOT_EXPECTED_ARCHIVE -ErrorAction SilentlyContinue
    }
    if ($hadEnginePrefix) {
        $env:RUNTIME_OOT_ENGINE_PREFIX = $previousEnginePrefix
    } else {
        Remove-Item Env:RUNTIME_OOT_ENGINE_PREFIX -ErrorAction SilentlyContinue
    }
    if (Test-Path -LiteralPath $testRoot) {
        Remove-Item -LiteralPath $testRoot -Recurse -Force
    }
}

Write-Output "runtime out-of-tree smoke contract: OK"
