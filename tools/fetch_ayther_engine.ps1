<#
.SYNOPSIS
Select, download, and verify the AYTHER Engine artifact pinned by Runtime.

.DESCRIPTION
The lock file is the only source of release URLs and checksums. The script
validates the complete supported artifact matrix before selecting an entry.
Existing files are never overwritten: a matching file is reused after hash
verification, while a mismatching file makes the command fail.
#>
[CmdletBinding()]
param(
    [string]$LockFile = (Join-Path $PSScriptRoot "../dependencies/ayther-engine.lock.json"),

    [ValidateSet("windows", "linux")]
    [string]$Platform,

    [ValidateSet("x86_64")]
    [string]$Architecture = "x86_64",

    [ValidateSet("engine", "engine-vpx")]
    [string]$Variant = "engine",

    [string]$DestinationDirectory = (Join-Path $PSScriptRoot "../.deps"),

    [string]$ArchivePath,

    [switch]$ValidateOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Read-AytherEngineLock {
    param([Parameter(Mandatory)][string]$Path)

    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $lock = Get-Content -LiteralPath $resolved -Raw | ConvertFrom-Json

    if ($lock.schemaVersion -ne 1) {
        throw "Unsupported AYTHER Engine lock schema: $($lock.schemaVersion)"
    }
    if ($lock.dependency -ne "AYTHER Engine") {
        throw "The lock does not describe AYTHER Engine."
    }
    if ($lock.repository -ne "https://github.com/Ayther-Dev/AYTHER-Engine") {
        throw "Unexpected AYTHER Engine repository: $($lock.repository)"
    }
    if ($lock.release.tag -notmatch '^v[0-9]+\.[0-9]+\.[0-9]+-rc\.[0-9]+$') {
        throw "Invalid pinned release tag: $($lock.release.tag)"
    }
    $expectedReleaseUrl = "$($lock.repository)/releases/tag/$($lock.release.tag)"
    if ($lock.release.url -cne $expectedReleaseUrl) {
        throw "Release URL does not match repository and tag: $($lock.release.url)"
    }

    $expectedKeys = @(
        "linux/x86_64/engine",
        "windows/x86_64/engine",
        "linux/x86_64/engine-vpx",
        "windows/x86_64/engine-vpx"
    )
    $actualKeys = [System.Collections.Generic.List[string]]::new()
    $fileNames = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::Ordinal)

    foreach ($artifact in @($lock.artifacts)) {
        $key = "$($artifact.platform)/$($artifact.architecture)/$($artifact.variant)"
        $actualKeys.Add($key)
        if (-not $fileNames.Add([string]$artifact.fileName)) {
            throw "Duplicate artifact filename in lock: $($artifact.fileName)"
        }
        if ($artifact.sha256 -notmatch '^[0-9a-f]{64}$') {
            throw "Invalid SHA-256 for $($artifact.fileName)."
        }

        $prefix = if ($artifact.variant -eq "engine-vpx") {
            "ayther-engine-vpx"
        } elseif ($artifact.variant -eq "engine") {
            "ayther-engine"
        } else {
            throw "Unsupported artifact variant: $($artifact.variant)"
        }
        $expectedName = "$prefix-$($lock.release.tag)-$($artifact.platform)-$($artifact.architecture).zip"
        if ($artifact.fileName -cne $expectedName) {
            throw "Artifact filename does not match its coordinates: $($artifact.fileName)"
        }
        if ([bool]$artifact.vpx -ne ($artifact.variant -eq "engine-vpx")) {
            throw "VPX flag does not match variant for $($artifact.fileName)."
        }

        $uri = [Uri]$artifact.url
        if ($uri.Scheme -ne "https" -or $uri.Host -ne "github.com") {
            throw "Artifact URL must use HTTPS on github.com: $($artifact.url)"
        }
        $expectedSuffix = "/releases/download/$($lock.release.tag)/$($artifact.fileName)"
        if (-not $uri.AbsolutePath.EndsWith($expectedSuffix, [StringComparison]::Ordinal)) {
            throw "Artifact URL does not match the pinned release: $($artifact.url)"
        }
    }

    if ($actualKeys.Count -ne $expectedKeys.Count) {
        throw "The AYTHER Engine lock must contain exactly four supported artifacts."
    }
    foreach ($expectedKey in $expectedKeys) {
        if (@($actualKeys | Where-Object { $_ -eq $expectedKey }).Count -ne 1) {
            throw "Missing or duplicate artifact coordinates: $expectedKey"
        }
    }

    return $lock
}

function Get-LockedArtifact {
    param(
        [Parameter(Mandatory)]$Lock,
        [Parameter(Mandatory)][string]$TargetPlatform,
        [Parameter(Mandatory)][string]$TargetArchitecture,
        [Parameter(Mandatory)][string]$TargetVariant
    )

    $matches = @($Lock.artifacts | Where-Object {
        $_.platform -eq $TargetPlatform -and
        $_.architecture -eq $TargetArchitecture -and
        $_.variant -eq $TargetVariant
    })
    if ($matches.Count -ne 1) {
        throw "No unique locked artifact for $TargetPlatform/$TargetArchitecture/$TargetVariant."
    }
    return $matches[0]
}

function Assert-ArtifactHash {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)]$Artifact,
        [switch]$RequireLockedName
    )

    $resolved = (Resolve-Path -LiteralPath $Path).Path
    if ($RequireLockedName -and
        (Split-Path -Leaf $resolved) -cne [string]$Artifact.fileName) {
        throw "Archive name does not match lock: expected $($Artifact.fileName)."
    }
    $actual = (Get-FileHash -LiteralPath $resolved -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -cne [string]$Artifact.sha256) {
        throw "AYTHER Engine checksum mismatch for $resolved. Expected $($Artifact.sha256), got $actual."
    }
    return $resolved
}

$lock = Read-AytherEngineLock -Path $LockFile

if ($ValidateOnly) {
    if ($ArchivePath) {
        throw "-ValidateOnly and -ArchivePath cannot be used together."
    }
    Write-Output "AYTHER Engine lock valid: $($lock.release.tag) ($(@($lock.artifacts).Count) artifacts)"
    return
}

if (-not $Platform) {
    if ([Runtime.InteropServices.RuntimeInformation]::IsOSPlatform(
            [Runtime.InteropServices.OSPlatform]::Windows)) {
        $Platform = "windows"
    } elseif ([Runtime.InteropServices.RuntimeInformation]::IsOSPlatform(
            [Runtime.InteropServices.OSPlatform]::Linux)) {
        $Platform = "linux"
    } else {
        throw "This lock has no artifact for the current operating system; pass -Platform explicitly."
    }
}

$artifact = Get-LockedArtifact `
    -Lock $lock `
    -TargetPlatform $Platform `
    -TargetArchitecture $Architecture `
    -TargetVariant $Variant

if ($ArchivePath) {
    $verified = Assert-ArtifactHash -Path $ArchivePath -Artifact $artifact -RequireLockedName
    Write-Output "AYTHER Engine artifact verified: $verified"
    return
}

$destinationRoot = [IO.Path]::GetFullPath($DestinationDirectory)
[IO.Directory]::CreateDirectory($destinationRoot) | Out-Null
$destination = Join-Path $destinationRoot $artifact.fileName

if (Test-Path -LiteralPath $destination) {
    $verified = Assert-ArtifactHash -Path $destination -Artifact $artifact -RequireLockedName
    Write-Output "AYTHER Engine artifact already present and verified: $verified"
    return
}

$temporary = Join-Path $destinationRoot ".download-$([guid]::NewGuid().ToString('N')).partial"
try {
    Invoke-WebRequest -Uri $artifact.url -OutFile $temporary
    Assert-ArtifactHash -Path $temporary -Artifact $artifact | Out-Null
    Move-Item -LiteralPath $temporary -Destination $destination
} finally {
    if (Test-Path -LiteralPath $temporary) {
        Remove-Item -LiteralPath $temporary -Force
    }
}

Write-Output "AYTHER Engine artifact downloaded and verified: $destination"
