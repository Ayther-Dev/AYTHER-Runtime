<#
.SYNOPSIS
Validate and extract the Engine package pinned for audio and replay QA.

.DESCRIPTION
This bootstrap is deliberately separate from the release bootstrap of the
ordinary Runtime. It accepts two kinds of QA lock:

- `qa-development`: a local development package (`officialRelease: false`)
  named by a path relative to the lock. It is verified with its SHA-256 and
  its content manifest; it is never downloaded and no release provenance is
  attributed to it.
- `qa-release` (spec 002, BR-174): the `engine-vpx` Windows artifact of a
  published Engine release (`officialRelease: true`), named by its release
  asset URL with its SHA-256, the release `CHECKSUMS.sha256` and its provenance
  attestation. It never points to a local path. The archive is downloaded,
  checked against the lock and the published checksums, and verified with
  `gh attestation verify` before anything is extracted. The release publishes
  no content manifest: the bootstrap derives one from the verified archive when
  it extracts it and checks the cached prefix against it. A `file:` URL has no
  attestation; it is accepted only with -AllowLocalArtifactForTest, for the
  local test of the bootstrap.

It prints the extracted CMake prefix.
#>
[CmdletBinding()]
param(
    [string]$LockFile = (Join-Path $PSScriptRoot '../dependencies/ayther-engine.qa.lock.json'),
    [string]$DestinationDirectory = (Join-Path $PSScriptRoot '../.deps/ayther-engine-qa'),
    [switch]$ValidateOnly,
    [switch]$AllowLocalArtifactForTest
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
        ($lock.selection -cne 'qa-development' -and $lock.selection -cne 'qa-release')) {
        throw 'Unsupported AYTHER Engine QA lock identity.'
    }
    $release = $lock.selection -ceq 'qa-release'
    $expectedKind = if ($release) { 'release' } else { 'development' }
    if ($lock.package.kind -cne $expectedKind -or
        [bool]$lock.package.officialRelease -ne $release -or
        $lock.package.id -notmatch '^[a-z0-9][a-z0-9._-]+$' -or
        $lock.package.productVersion -notmatch '^\d+\.\d+\.\d+$' -or
        $lock.package.audioObservationContract -notmatch '^\d+\.\d+$') {
        throw 'Invalid AYTHER Engine QA package metadata.'
    }
    if ($lock.artifact.platform -cne 'windows' -or
        $lock.artifact.architecture -cne 'x86_64' -or
        $lock.artifact.variant -cne 'engine-vpx' -or
        $lock.artifact.archiveRoot -cne $lock.package.id -or
        $lock.artifact.sha256 -notmatch '^[0-9a-f]{64}$' -or
        (-not $release -and $lock.artifact.contentManifestSha256 -notmatch '^[0-9a-f]{64}$')) {
        throw 'Invalid AYTHER Engine QA artifact coordinates.'
    }
    if ($release) {
        # A published artifact is named by URL and its provenance, never by a local path.
        if ($lock.artifact.PSObject.Properties['relativePath'] -or
            $lock.artifact.PSObject.Properties['contentManifestRelativePath']) {
            throw 'A qa-release lock must not name a local path.'
        }
        $releaseTag = if ($lock.PSObject.Properties['release'] -and
            $lock.release.PSObject.Properties['tag']) { [string]$lock.release.tag } else { '' }
        $text = { param($section, $name) if ($lock.PSObject.Properties[$section] -and
                $lock.$section.PSObject.Properties[$name]) { [string]$lock.$section.$name } else { '' } }
        if ($releaseTag -notmatch '^v\d+\.\d+\.\d+(-rc\.\d+)?$' -or
            $lock.package.id -cne "ayther-engine-vpx-$releaseTag-windows-x86_64" -or
            (& $text 'release' 'checksumsSha256') -notmatch '^[0-9a-f]{64}$') {
            throw 'Invalid qa-release identity: the package is the engine-vpx Windows artifact of the release.'
        }
        $urls = [ordered]@{
            url = (& $text 'artifact' 'url')
            checksumsUrl = (& $text 'release' 'checksumsUrl')
        }
        foreach ($field in $urls.Keys) {
            $uri = $null
            if (-not [Uri]::TryCreate($urls[$field], [UriKind]::Absolute, [ref]$uri) -or
                ($uri.Scheme -cne 'https' -and $uri.Scheme -cne 'file')) {
                throw "Invalid qa-release $field."
            }
        }
        $attestation = $lock.PSObject.Properties['attestation']
        $field = { param($name) if ($attestation -and $lock.attestation.PSObject.Properties[$name]) {
                [string]$lock.attestation.$name } else { '' } }
        if (-not $attestation -or
            (& $field 'repository') -notmatch '^[A-Za-z0-9-]+/[A-Za-z0-9._-]+$' -or
            (& $field 'signerWorkflow') -notmatch '^[A-Za-z0-9-]+/[A-Za-z0-9._-]+/\.github/workflows/[A-Za-z0-9._-]+\.ya?ml$' -or
            (& $field 'sourceRef') -cne "refs/tags/$releaseTag" -or
            (& $field 'predicateType') -cne 'https://slsa.dev/provenance/v1') {
            throw 'A qa-release lock must declare the attestation of the published artifact.'
        }
        # A published artifact is an asset of the locked release of the attested repository.
        $download = "https://github.com/$(& $field 'repository')/releases/download/$releaseTag"
        if (([Uri]$urls.url).Scheme -ceq 'https' -and
            $urls.url -cne "$download/$($lock.package.id).zip") {
            throw "The qa-release artifact is not the release asset $download/$($lock.package.id).zip."
        }
        if (([Uri]$urls.checksumsUrl).Scheme -ceq 'https' -and
            $urls.checksumsUrl -cne "$download/CHECKSUMS.sha256") {
            throw "The qa-release checksums are not the release asset $download/CHECKSUMS.sha256."
        }
    } elseif ([IO.Path]::IsPathRooted([string]$lock.artifact.relativePath) -or
              [IO.Path]::IsPathRooted([string]$lock.artifact.contentManifestRelativePath)) {
        throw 'Invalid AYTHER Engine QA artifact coordinates.'
    }
    if ($release) {
        return [pscustomobject]@{ Lock = $lock; Path = $resolved }
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
        (Join-Path $Prefix 'include/ayther/engine/render_observer.hpp'),
        (Join-Path $Prefix 'include/ayther/engine/visual_state.hpp'),
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

function Save-Artifact {
    param(
        [Parameter(Mandatory)][string]$Uri,
        [Parameter(Mandatory)][string]$Destination,
        [Parameter(Mandatory)][string]$Expected,
        [Parameter(Mandatory)][string]$Description
    )

    if (Test-Path -LiteralPath $Destination -PathType Leaf) {
        Assert-Sha256 -Path $Destination -Expected $Expected -Description $Description
        return
    }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
    $temporary = "$Destination.$([guid]::NewGuid().ToString('N')).partial"
    try {
        $source = [Uri]$Uri
        if ($source.IsFile) {
            Copy-Item -LiteralPath $source.LocalPath -Destination $temporary
        } else {
            Invoke-WebRequest -Uri $Uri -OutFile $temporary
        }
        Assert-Sha256 -Path $temporary -Expected $Expected -Description $Description
        Move-Item -LiteralPath $temporary -Destination $Destination
    } finally {
        if (Test-Path -LiteralPath $temporary) {
            Remove-Item -LiteralPath $temporary -Force
        }
    }
}

function Assert-PublishedChecksum {
    param(
        [Parameter(Mandatory)][string]$Checksums,
        [Parameter(Mandatory)]$Lock
    )

    $name = "$([string]$Lock.package.id).zip"
    $published = @(Get-Content -LiteralPath $Checksums | ForEach-Object {
            if ($_ -match '^([0-9a-f]{64}) [ *](.+)$' -and $Matches[2] -ceq $name) { $Matches[1] }
        })
    if ($published.Count -ne 1 -or $published[0] -cne [string]$Lock.artifact.sha256) {
        throw "The release checksums have not published the locked SHA-256 of $name."
    }
}

function Write-ContentManifest {
    param(
        [Parameter(Mandatory)][string]$Prefix,
        [Parameter(Mandatory)][string]$Destination
    )

    $lines = @(Get-ChildItem -LiteralPath $Prefix -Recurse -File | Sort-Object FullName |
        ForEach-Object {
            $relative = [IO.Path]::GetRelativePath($Prefix, $_.FullName).Replace('\', '/')
            "$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant())  $relative"
        })
    Set-Content -LiteralPath $Destination -Value $lines -Encoding utf8NoBOM
}

function Assert-Attestation {
    param(
        [Parameter(Mandatory)][string]$Archive,
        [Parameter(Mandatory)]$Lock
    )

    if (([Uri][string]$Lock.artifact.url).IsFile) {
        if (-not $AllowLocalArtifactForTest) {
            throw 'A local QA artifact has no attestation to verify; only -AllowLocalArtifactForTest accepts it.'
        }
        Write-Host '  [TEST] local QA artifact accepted without attestation'
        return
    }
    $gh = Get-Command gh -ErrorAction SilentlyContinue
    if (-not $gh) {
        throw "GitHub CLI 'gh' is required to verify the QA Engine attestation."
    }
    & $gh.Source attestation verify $Archive `
        --repo $Lock.attestation.repository `
        --signer-workflow $Lock.attestation.signerWorkflow `
        --source-ref $Lock.attestation.sourceRef `
        --predicate-type $Lock.attestation.predicateType `
        --deny-self-hosted-runners | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "SLSA provenance verification failed for '$Archive'."
    }
}

$selection = Read-QaLock -Path $LockFile
$lock = $selection.Lock
$lockDirectory = Split-Path -Parent $selection.Path
$isRelease = $lock.selection -ceq 'qa-release'
# A relative cache directory belongs to the repository, whatever the caller's working directory.
$destination = if ([IO.Path]::IsPathRooted($DestinationDirectory)) {
    [IO.Path]::GetFullPath($DestinationDirectory)
} else {
    [IO.Path]::GetFullPath((Join-Path (Join-Path $PSScriptRoot '..') $DestinationDirectory))
}
if ($isRelease) {
    if ($ValidateOnly) {
        Write-Host "AYTHER Engine QA release lock valid: $($lock.package.id)"
        return
    }
    $downloads = Resolve-ChildPath `
        -Path (Join-Path $destination "downloads/$([string]$lock.artifact.sha256)") `
        -Root $destination -Description 'QA Engine download cache'
    $archive = Join-Path $downloads "$([string]$lock.artifact.archiveRoot).zip"
    $checksums = Join-Path $downloads 'CHECKSUMS.sha256'
    Save-Artifact -Uri ([string]$lock.artifact.url) -Destination $archive `
        -Expected ([string]$lock.artifact.sha256) -Description 'QA Engine archive'
    Save-Artifact -Uri ([string]$lock.release.checksumsUrl) -Destination $checksums `
        -Expected ([string]$lock.release.checksumsSha256) -Description 'Engine release checksums'
    Assert-PublishedChecksum -Checksums $checksums -Lock $lock
    Assert-Attestation -Archive $archive -Lock $lock
} else {
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
}

$cache = Resolve-ChildPath `
    -Path (Join-Path $destination ([string]$lock.artifact.sha256)) `
    -Root $destination -Description 'QA Engine cache'
$prefix = Resolve-ChildPath `
    -Path (Join-Path $cache ([string]$lock.artifact.archiveRoot)) `
    -Root $cache -Description 'QA Engine prefix'
if ($isRelease) {
    # Derived from the verified archive when it was extracted.
    $contentManifest = Join-Path $cache "$([string]$lock.artifact.archiveRoot).contents.sha256"
}
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
    if ($isRelease) {
        $contentManifest = Join-Path $temporary "$([string]$lock.artifact.archiveRoot).contents.sha256"
        Write-ContentManifest -Prefix $extractedPrefix -Destination $contentManifest
    }
    Assert-QaPrefix -Prefix $extractedPrefix -Lock $lock `
        -ContentManifest $contentManifest
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    Move-Item -LiteralPath $temporary -Destination $cache
} finally {
    if (Test-Path -LiteralPath $temporary) {
        Remove-Item -LiteralPath $temporary -Recurse -Force
    }
}

Write-Host '  [ OK ] QA Engine package extracted and verified'
Write-Output $prefix
