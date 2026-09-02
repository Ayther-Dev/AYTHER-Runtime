<#
.SYNOPSIS
Bootstrap the AYTHER Engine package pinned by Runtime.

.DESCRIPTION
The lock file is the only source of release URLs and checksums. The script
selects the artifact for the host, downloads it, verifies the pinned and
published SHA-256 values, verifies GitHub's SLSA provenance, and extracts the
package into a dependency directory. Its only success-stream output is the
absolute package prefix, so callers can pass it directly to CMAKE_PREFIX_PATH.

Existing downloads and prefixes are never overwritten. A valid cached result
is reused after verification; an inconsistent cache makes the command fail.

.EXAMPLE
$enginePrefix = & ./tools/bootstrap_ayther_engine.ps1
cmake -S . -B build "-DCMAKE_PREFIX_PATH=$enginePrefix"
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

    [string]$DestinationDirectory = (Join-Path $PSScriptRoot "../.deps/ayther-engine"),

    [string]$ArchivePath,

    [switch]$ValidateOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-NormalizedPath {
    param([Parameter(Mandatory)][string]$Path)

    return [IO.Path]::GetFullPath($Path)
}

function Invoke-Download {
    param(
        [Parameter(Mandatory)][string]$Uri,
        [Parameter(Mandatory)][string]$Destination,
        [Parameter(Mandatory)][string]$ExpectedHash,
        [Parameter(Mandatory)][string]$Description
    )

    $temporary = "$Destination.$([guid]::NewGuid().ToString('N')).partial"
    try {
        Invoke-WebRequest -Uri $Uri -OutFile $temporary
        Assert-FileHash `
            -Path $temporary `
            -Expected $ExpectedHash `
            -Description $Description
        Move-Item -LiteralPath $temporary -Destination $Destination
    } finally {
        if (Test-Path -LiteralPath $temporary) {
            Remove-Item -LiteralPath $temporary -Force
        }
    }
}

function Assert-FileHash {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Expected,
        [Parameter(Mandatory)][string]$Description
    )

    $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -cne $Expected) {
        throw "$Description checksum mismatch for '$Path'. Expected $Expected, got $actual."
    }
}

function Read-AytherEngineLock {
    param([Parameter(Mandatory)][string]$Path)

    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $lock = Get-Content -LiteralPath $resolved -Raw | ConvertFrom-Json

    if ($lock.schemaVersion -ne 2) {
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

    $checksums = $lock.release.checksums
    if ($checksums.fileName -cne "CHECKSUMS.sha256") {
        throw "Unexpected release checksum filename: $($checksums.fileName)"
    }
    $expectedChecksumsUrl = "$($lock.repository)/releases/download/$($lock.release.tag)/$($checksums.fileName)"
    if ($checksums.url -cne $expectedChecksumsUrl) {
        throw "Checksum URL does not match the pinned release: $($checksums.url)"
    }
    if ($checksums.sha256 -notmatch '^[0-9a-f]{64}$') {
        throw "Invalid SHA-256 for $($checksums.fileName)."
    }

    $repositorySlug = ([Uri]$lock.repository).AbsolutePath.Trim('/')
    if ($lock.attestation.repository -cne $repositorySlug) {
        throw "Attestation repository does not match the release repository."
    }
    $expectedWorkflow = "$repositorySlug/.github/workflows/release.yml"
    if ($lock.attestation.signerWorkflow -cne $expectedWorkflow) {
        throw "Unexpected attestation signer workflow: $($lock.attestation.signerWorkflow)"
    }
    if ($lock.attestation.sourceRef -cne "refs/tags/$($lock.release.tag)") {
        throw "Attestation source ref does not match the pinned tag."
    }
    if ($lock.attestation.predicateType -cne "https://slsa.dev/provenance/v1") {
        throw "Unexpected attestation predicate: $($lock.attestation.predicateType)"
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
    Assert-FileHash `
        -Path $resolved `
        -Expected ([string]$Artifact.sha256) `
        -Description "AYTHER Engine artifact"
    return $resolved
}

function Assert-PublishedChecksum {
    param(
        [Parameter(Mandatory)][string]$ChecksumsPath,
        [Parameter(Mandatory)]$Artifact
    )

    $escapedName = [regex]::Escape([string]$Artifact.fileName)
    $matches = @(Get-Content -LiteralPath $ChecksumsPath | Where-Object {
        $_ -match "^([0-9a-f]{64})\s+\*?$escapedName$"
    })
    if ($matches.Count -ne 1) {
        throw "Published checksums do not contain one entry for '$($Artifact.fileName)'."
    }
    $published = ([regex]::Match($matches[0], '^([0-9a-f]{64})')).Groups[1].Value
    if ($published -cne [string]$Artifact.sha256) {
        throw "Published checksum for '$($Artifact.fileName)' differs from the Runtime lock."
    }
}

function Assert-Attestation {
    param(
        [Parameter(Mandatory)][string]$Archive,
        [Parameter(Mandatory)]$Attestation
    )

    $gh = Get-Command gh -ErrorAction SilentlyContinue
    if (-not $gh) {
        throw "GitHub CLI 'gh' is required to verify the Engine attestation."
    }

    & $gh.Source attestation verify $Archive `
        --repo $Attestation.repository `
        --signer-workflow $Attestation.signerWorkflow `
        --source-ref $Attestation.sourceRef `
        --predicate-type $Attestation.predicateType `
        --deny-self-hosted-runners | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "SLSA provenance verification failed for '$Archive'."
    }
}

function Assert-EnginePrefix {
    param([Parameter(Mandatory)][string]$Prefix)

    $required = @(
        "include/ayther/engine/capabilities.hpp",
        "include/ayther/engine/engine.hpp",
        "lib/cmake/Ayther/AytherConfig.cmake",
        "lib/cmake/Ayther/AytherConfigVersion.cmake",
        "lib/cmake/Ayther/AytherEngineTargets.cmake",
        "share/Ayther/shaders/sprite.frag.spv"
    )
    foreach ($relativePath in $required) {
        if (-not (Test-Path -LiteralPath (Join-Path $Prefix $relativePath) -PathType Leaf)) {
            throw "Extracted Engine package is missing '$relativePath'."
        }
    }

    $libraries = @(Get-ChildItem -LiteralPath (Join-Path $Prefix "lib") -File |
        Where-Object { $_.Name -match '^(lib)?ayther_engine\.(lib|a)$' })
    if ($libraries.Count -ne 1) {
        throw "Extracted Engine package does not contain one engine static library."
    }
}

$lock = Read-AytherEngineLock -Path $LockFile

if ($ValidateOnly) {
    if ($ArchivePath) {
        throw "-ValidateOnly and -ArchivePath cannot be used together."
    }
    Write-Host "AYTHER Engine lock valid: $($lock.release.tag) ($(@($lock.artifacts).Count) artifacts)"
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

$hostArchitecture = [Runtime.InteropServices.RuntimeInformation]::OSArchitecture
if ($hostArchitecture -ne [Runtime.InteropServices.Architecture]::X64) {
    throw "The Engine lock has no native artifact for host architecture '$hostArchitecture'."
}

$artifact = Get-LockedArtifact `
    -Lock $lock `
    -TargetPlatform $Platform `
    -TargetArchitecture $Architecture `
    -TargetVariant $Variant

$destinationRoot = Get-NormalizedPath -Path $DestinationDirectory
$downloadRoot = Join-Path $destinationRoot "downloads/$($lock.release.tag)"
$prefixRoot = Join-Path $destinationRoot "prefixes"
[IO.Directory]::CreateDirectory($downloadRoot) | Out-Null
[IO.Directory]::CreateDirectory($prefixRoot) | Out-Null

if ($ArchivePath) {
    $archive = Assert-ArtifactHash -Path $ArchivePath -Artifact $artifact -RequireLockedName
    Write-Host "  [ OK ] supplied archive matches the Runtime lock"
} else {
    $archive = Join-Path $downloadRoot $artifact.fileName
    if (-not (Test-Path -LiteralPath $archive)) {
        Write-Host "Downloading $($artifact.fileName)..."
        Invoke-Download `
            -Uri $artifact.url `
            -Destination $archive `
            -ExpectedHash ([string]$artifact.sha256) `
            -Description "AYTHER Engine artifact"
    }
    $archive = Assert-ArtifactHash -Path $archive -Artifact $artifact -RequireLockedName
    Write-Host "  [ OK ] archive SHA-256 matches the Runtime lock"
}

$checksumsPath = Join-Path $downloadRoot $lock.release.checksums.fileName
if (-not (Test-Path -LiteralPath $checksumsPath)) {
    Write-Host "Downloading $($lock.release.checksums.fileName)..."
    Invoke-Download `
        -Uri $lock.release.checksums.url `
        -Destination $checksumsPath `
        -ExpectedHash ([string]$lock.release.checksums.sha256) `
        -Description "Release checksum manifest"
}
Assert-FileHash `
    -Path $checksumsPath `
    -Expected ([string]$lock.release.checksums.sha256) `
    -Description "Release checksum manifest"
Assert-PublishedChecksum -ChecksumsPath $checksumsPath -Artifact $artifact
Write-Host "  [ OK ] published and locked SHA-256 values agree"

Assert-Attestation -Archive $archive -Attestation $lock.attestation
Write-Host "  [ OK ] SLSA provenance matches the release workflow and tag"

$archiveRootName = [IO.Path]::GetFileNameWithoutExtension([string]$artifact.fileName)
$prefix = Join-Path $prefixRoot $archiveRootName
$markerName = ".ayther-runtime-bootstrap.json"
$marker = Join-Path $prefix $markerName
if (Test-Path -LiteralPath $prefix) {
    if (-not (Test-Path -LiteralPath $marker -PathType Leaf)) {
        throw "Cached prefix '$prefix' has no bootstrap marker; refusing to trust or overwrite it."
    }
    $cached = Get-Content -LiteralPath $marker -Raw | ConvertFrom-Json
    if ($cached.archive -cne [string]$artifact.fileName -or
        $cached.sha256 -cne [string]$artifact.sha256) {
        throw "Cached prefix '$prefix' does not match the selected Engine artifact."
    }
    Assert-EnginePrefix -Prefix $prefix
    Write-Host "  [ OK ] cached Engine prefix is complete"
    Write-Output (Get-NormalizedPath -Path $prefix)
    return
}

$staging = Join-Path $destinationRoot ".extract-$([guid]::NewGuid().ToString('N'))"
try {
    [IO.Directory]::CreateDirectory($staging) | Out-Null
    Expand-Archive -LiteralPath $archive -DestinationPath $staging
    $extracted = Join-Path $staging $archiveRootName
    if (-not (Test-Path -LiteralPath $extracted -PathType Container)) {
        throw "Engine archive did not extract to the expected root '$archiveRootName'."
    }
    $unexpected = @(Get-ChildItem -LiteralPath $staging | Where-Object Name -cne $archiveRootName)
    if ($unexpected.Count -ne 0) {
        throw "Engine archive contains unexpected top-level entries."
    }
    Assert-EnginePrefix -Prefix $extracted

    [ordered]@{
        schemaVersion = 1
        release = [string]$lock.release.tag
        archive = [string]$artifact.fileName
        sha256 = [string]$artifact.sha256
        attestationRepository = [string]$lock.attestation.repository
        attestationSourceRef = [string]$lock.attestation.sourceRef
    } | ConvertTo-Json | Set-Content `
        -LiteralPath (Join-Path $extracted $markerName) `
        -Encoding utf8NoBOM

    [IO.Directory]::Move($extracted, $prefix)
} finally {
    if (Test-Path -LiteralPath $staging) {
        Remove-Item -LiteralPath $staging -Recurse -Force
    }
}

Write-Host "  [ OK ] Engine package extracted"
Write-Output (Get-NormalizedPath -Path $prefix)
