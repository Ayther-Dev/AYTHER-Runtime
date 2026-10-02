<#
.SYNOPSIS
Validate and extract the local Engine development package pinned for audio QA.

.DESCRIPTION
This bootstrap is deliberately separate from the attested release bootstrap.
It accepts only a development lock with `officialRelease: false`, verifies the
local archive and its internal content manifest, and prints the extracted CMake
prefix. It never downloads or attributes release provenance to the package.
#>
[CmdletBinding()]
param(
    [string]$LockFile = (Join-Path $PSScriptRoot '../dependencies/ayther-engine.qa.lock.json'),
    [string]$DestinationDirectory = (Join-Path $PSScriptRoot '../.deps/ayther-engine-qa'),
    [switch]$ValidateOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-Sha256 {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Expected,
        [Parameter(Mandatory)][string]$Description
    )

    $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -cne $Expected) {
        throw "$Description checksum mismatch. Expected $Expected, got $actual."
    }
}

function Resolve-ChildPath {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Root,
        [Parameter(Mandatory)][string]$Description
    )

    $resolvedPath = [IO.Path]::GetFullPath($Path)
    $resolvedRoot = [IO.Path]::GetFullPath($Root).TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar
    )
    $rootPrefix = $resolvedRoot + [IO.Path]::DirectorySeparatorChar
    if (-not $resolvedPath.StartsWith(
            $rootPrefix,
            [StringComparison]::OrdinalIgnoreCase
        )) {
        throw "$Description escapes its declared root '$resolvedRoot': $resolvedPath"
    }
    return $resolvedPath
}

function Read-QaLock {
    param([Parameter(Mandatory)][string]$Path)

    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $lock = Get-Content -LiteralPath $resolved -Raw | ConvertFrom-Json
    if ($lock.schemaVersion -ne 2 -or $lock.dependency -cne 'AYTHER Engine' -or
        $lock.selection -cne 'qa-development') {
        throw 'Unsupported AYTHER Engine QA lock identity.'
    }
    if ($lock.package.kind -cne 'development' -or
        [bool]$lock.package.officialRelease -or
        $lock.package.id -notmatch '^[a-z0-9][a-z0-9-]+$' -or
        $lock.package.productVersion -notmatch '^\d+\.\d+\.\d+$' -or
        $lock.package.audioObservationContract -notmatch '^\d+\.\d+$') {
        throw 'Invalid AYTHER Engine QA package metadata.'
    }
    if ($lock.artifact.platform -cne 'windows' -or
        $lock.artifact.architecture -cne 'x86_64' -or
        $lock.artifact.variant -cne 'engine-vpx' -or
        $lock.artifact.archiveRoot -cne $lock.package.id -or
        $lock.artifact.sha256 -notmatch '^[0-9a-f]{64}$' -or
        $lock.artifact.contentManifestSha256 -notmatch '^[0-9a-f]{64}$' -or
        [IO.Path]::IsPathRooted([string]$lock.artifact.relativePath) -or
        [IO.Path]::IsPathRooted([string]$lock.artifact.contentManifestRelativePath)) {
        throw 'Invalid AYTHER Engine QA artifact coordinates.'
    }
    if ($lock.referenceRelease.tag -cne $lock.package.baseReferenceTag -or
        $lock.referenceRelease.windowsEngineSha256 -notmatch '^[0-9a-f]{64}$' -or
        $lock.referenceRelease.windowsEngineVpxSha256 -notmatch '^[0-9a-f]{64}$' -or
        $lock.artifact.sha256 -ceq $lock.referenceRelease.windowsEngineSha256 -or
        $lock.artifact.sha256 -ceq $lock.referenceRelease.windowsEngineVpxSha256) {
        throw 'QA artifact and reference-release identities are not separated.'
    }
    return [pscustomobject]@{ Lock = $lock; Path = $resolved }
}

function Assert-QaPrefix {
    param(
        [Parameter(Mandatory)][string]$Prefix,
        [Parameter(Mandatory)]$Lock,
        [Parameter(Mandatory)][string]$ContentManifest
    )

    foreach ($required in @(
        (Join-Path $Prefix 'include/ayther/engine/audio_observer.hpp'),
        (Join-Path $Prefix 'include/ayther/engine/audio_production_limit.hpp'),
        (Join-Path $Prefix 'include/vpx/vpx_decoder.h'),
        (Join-Path $Prefix 'lib/cmake/Ayther/AytherConfig.cmake'),
        (Join-Path $Prefix 'lib/cmake/Ayther/AytherEngineTargets.cmake')
    )) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
            throw "QA Engine prefix is missing '$required'."
        }
    }

    $manifestEntries = [System.Collections.Generic.HashSet[string]]::new(
        [StringComparer]::Ordinal)
    foreach ($line in Get-Content -LiteralPath $ContentManifest) {
        if ($line -notmatch '^([0-9a-f]{64})  (.+)$') {
            throw "Invalid QA Engine content entry: $line"
        }
        $expected = $Matches[1]
        $relative = $Matches[2]
        if ([IO.Path]::IsPathRooted($relative)) {
            throw "QA Engine content entry is rooted: $relative"
        }
        $file = Resolve-ChildPath -Path (Join-Path $Prefix $relative) `
            -Root $Prefix -Description 'QA Engine content entry'
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
            throw "QA Engine content is missing '$relative'."
        }
        Assert-Sha256 -Path $file -Expected $expected `
            -Description "QA Engine content '$relative'"
        if (-not $manifestEntries.Add($relative.Replace('\', '/'))) {
            throw "Duplicate QA Engine content entry: $relative"
        }
    }
    $actualEntries = @(Get-ChildItem -LiteralPath $Prefix -Recurse -File |
        ForEach-Object {
            [IO.Path]::GetRelativePath($Prefix, $_.FullName).Replace('\', '/')
        })
    foreach ($relative in $actualEntries) {
        if (-not $manifestEntries.Contains($relative)) {
            throw "QA Engine content is not declared by the manifest: $relative"
        }
    }
    if ($actualEntries.Count -ne $manifestEntries.Count) {
        throw 'QA Engine content manifest cardinality mismatch.'
    }
}

$selection = Read-QaLock -Path $LockFile
$lock = $selection.Lock
$lockDirectory = Split-Path -Parent $selection.Path
$archive = [IO.Path]::GetFullPath((Join-Path $lockDirectory $lock.artifact.relativePath))
$contentManifest = [IO.Path]::GetFullPath((Join-Path $lockDirectory `
    $lock.artifact.contentManifestRelativePath))
if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) {
    throw "Locked QA Engine archive is unavailable: $archive"
}
Assert-Sha256 -Path $archive -Expected ([string]$lock.artifact.sha256) `
    -Description 'QA Engine archive'
if (-not (Test-Path -LiteralPath $contentManifest -PathType Leaf)) {
    throw "Locked QA Engine content manifest is unavailable: $contentManifest"
}
Assert-Sha256 -Path $contentManifest `
    -Expected ([string]$lock.artifact.contentManifestSha256) `
    -Description 'QA Engine content manifest'

if ($ValidateOnly) {
    Write-Host "AYTHER Engine QA lock valid: $($lock.package.id)"
    return
}

$destination = [IO.Path]::GetFullPath($DestinationDirectory)
$cache = Resolve-ChildPath `
    -Path (Join-Path $destination ([string]$lock.artifact.sha256)) `
    -Root $destination -Description 'QA Engine cache'
$prefix = Resolve-ChildPath `
    -Path (Join-Path $cache ([string]$lock.artifact.archiveRoot)) `
    -Root $cache -Description 'QA Engine prefix'
if (Test-Path -LiteralPath $prefix -PathType Container) {
    Assert-QaPrefix -Prefix $prefix -Lock $lock -ContentManifest $contentManifest
    Write-Host '  [ OK ] cached QA Engine prefix verified'
    Write-Output $prefix
    return
}
if (Test-Path -LiteralPath $cache) {
    throw "Incomplete QA Engine cache already exists: $cache"
}

$temporary = Resolve-ChildPath `
    -Path "$cache.$([guid]::NewGuid().ToString('N')).partial" `
    -Root $destination -Description 'QA Engine temporary extraction directory'
New-Item -ItemType Directory -Path $temporary -Force | Out-Null
try {
    Expand-Archive -LiteralPath $archive -DestinationPath $temporary
    $entries = @(Get-ChildItem -LiteralPath $temporary)
    if ($entries.Count -ne 1 -or -not $entries[0].PSIsContainer -or
        $entries[0].Name -cne $lock.artifact.archiveRoot) {
        throw 'QA Engine archive has an unexpected top-level layout.'
    }
    $extractedPrefix = Resolve-ChildPath -Path $entries[0].FullName `
        -Root $temporary -Description 'Extracted QA Engine prefix'
    Assert-QaPrefix -Prefix $extractedPrefix -Lock $lock `
        -ContentManifest $contentManifest
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    Move-Item -LiteralPath $temporary -Destination $cache
} finally {
    if (Test-Path -LiteralPath $temporary) {
        Remove-Item -LiteralPath $temporary -Recurse -Force
    }
}

Write-Host '  [ OK ] QA Engine development package extracted and verified'
Write-Output $prefix
