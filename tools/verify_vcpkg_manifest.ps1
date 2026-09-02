<#
.SYNOPSIS
Verify that Runtime's vcpkg manifest covers the installed Engine package.

.DESCRIPTION
Reads AytherConfig.cmake as the source of Engine's native dependency closure,
maps its CMake package names to vcpkg ports, and requires every port as a
top-level Runtime dependency. It also protects Runtime-owned ImGui and stb plus
the SDL3/Vulkan backend features used by Runtime.
#>
[CmdletBinding()]
param(
    [string]$Manifest = (Join-Path $PSScriptRoot "../vcpkg.json"),

    [Parameter(Mandatory)]
    [string]$AytherConfig
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$manifestPath = (Resolve-Path -LiteralPath $Manifest).Path
$configPath = (Resolve-Path -LiteralPath $AytherConfig).Path
$manifestData = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json

if ($manifestData.'builtin-baseline' -notmatch '^[0-9a-f]{40}$') {
    throw "vcpkg.json must pin a 40-character lowercase builtin-baseline."
}

$dependencies = [System.Collections.Generic.Dictionary[string, object]]::new(
    [StringComparer]::OrdinalIgnoreCase)
foreach ($entry in @($manifestData.dependencies)) {
    if ($entry -is [string]) {
        $name = [string]$entry
        $features = @()
    } else {
        $name = [string]$entry.name
        $features = if ($entry.PSObject.Properties.Name -contains "features") {
            @($entry.features | ForEach-Object { [string]$_ })
        } else {
            @()
        }
    }
    if (-not $name) {
        throw "vcpkg.json contains a dependency without a name."
    }
    if ($dependencies.ContainsKey($name)) {
        throw "vcpkg.json contains duplicate dependency '$name'."
    }
    $dependencies.Add($name, [pscustomobject]@{
        Name = $name
        Features = $features
    })
}

$packageToPort = @{
    SDL3 = "sdl3"
    Vulkan = "vulkan"
    VulkanMemoryAllocator = "vulkan-memory-allocator"
    "vk-bootstrap" = "vk-bootstrap"
    tomlplusplus = "tomlplusplus"
    zstd = "zstd"
}
$systemPackages = @("Threads", "PkgConfig")
$configText = Get-Content -LiteralPath $configPath -Raw
$packageMatches = [regex]::Matches(
    $configText,
    '(?m)^\s*find_dependency\(\s*([A-Za-z0-9_.+-]+)')
$enginePorts = [System.Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase)

foreach ($match in $packageMatches) {
    $package = $match.Groups[1].Value
    if ($package -in $systemPackages) {
        continue
    }
    if (-not $packageToPort.ContainsKey($package)) {
        throw "No vcpkg port mapping is defined for Engine dependency '$package'."
    }
    $port = $packageToPort[$package]
    if (-not $dependencies.ContainsKey($port)) {
        throw "Engine dependency '$package' requires missing direct vcpkg port '$port'."
    }
    $enginePorts.Add($port) | Out-Null
}

if ($enginePorts.Count -eq 0) {
    throw "AytherConfig.cmake did not declare any mapped native dependencies."
}

foreach ($runtimePort in @("imgui", "stb")) {
    if (-not $dependencies.ContainsKey($runtimePort)) {
        throw "Runtime-owned dependency '$runtimePort' must remain direct in vcpkg.json."
    }
}

$requiredFeatures = @{
    sdl3 = @("vulkan")
    imgui = @("sdl3-binding", "vulkan-binding")
}
foreach ($port in $requiredFeatures.Keys) {
    foreach ($feature in $requiredFeatures[$port]) {
        if ($feature -notin @($dependencies[$port].Features)) {
            throw "Direct vcpkg dependency '$port' is missing required feature '$feature'."
        }
    }
}

$closure = @($enginePorts) | Sort-Object
Write-Output "vcpkg manifest valid: Engine=[$($closure -join ', ')]; Runtime=[imgui, stb]"
