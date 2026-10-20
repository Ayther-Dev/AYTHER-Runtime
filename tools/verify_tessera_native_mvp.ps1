[CmdletBinding()]
param(
    [string]$LockFile = (Join-Path $PSScriptRoot '../dependencies/tessera.native-mvp.lock.json'),
    [string]$ArchivePath,
    [string]$ChecksumsPath,
    [switch]$ValidateOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-Hash {
    param([string]$Path, [string]$Expected)
    $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -cne $Expected) { throw "SHA-256 mismatch for '$Path'." }
}

$lock = Get-Content -LiteralPath $LockFile -Raw | ConvertFrom-Json
if ($lock.schemaVersion -ne 1 -or $lock.dependency -cne 'AYTHER Tessera' -or
    $lock.repository -cne 'https://github.com/Ayther-Dev/AYTHER-Tessera') {
    throw 'Unexpected Tessera lock identity.'
}
$tag = [string]$lock.release.tag
$repo = [string]$lock.repository
$name = "tessera-$tag-windows-x86_64.zip"
if ($tag -notmatch '^v\d+\.\d+\.\d+-mvp\.\d+$' -or
    $lock.sourceCommit -notmatch '^[0-9a-f]{40}$' -or
    $lock.release.url -cne "$repo/releases/tag/$tag" -or
    $lock.release.checksums.fileName -cne 'CHECKSUMS.sha256' -or
    $lock.release.checksums.url -cne "$repo/releases/download/$tag/CHECKSUMS.sha256" -or
    $lock.release.checksums.sha256 -notmatch '^[0-9a-f]{64}$' -or
    $lock.artifact.platform -cne 'windows' -or
    $lock.artifact.architecture -cne 'x86_64' -or
    $lock.artifact.fileName -cne $name -or
    $lock.artifact.url -cne "$repo/releases/download/$tag/$name" -or
    $lock.artifact.sha256 -notmatch '^[0-9a-f]{64}$' -or
    $lock.attestation.repository -cne 'Ayther-Dev/AYTHER-Tessera' -or
    $lock.attestation.signerWorkflow -cne 'Ayther-Dev/AYTHER-Tessera/.github/workflows/native-release.yml' -or
    $lock.attestation.sourceRef -cne "refs/tags/$tag" -or
    $lock.attestation.predicateType -cne 'https://slsa.dev/provenance/v1') {
    throw 'Tessera lock coordinates or provenance are invalid.'
}
if ($ValidateOnly) {
    if ($ArchivePath -or $ChecksumsPath) { throw 'ValidateOnly cannot inspect archives.' }
    Write-Host "Tessera lock valid: $tag"
    return
}
if (-not $ArchivePath -or -not $ChecksumsPath) {
    throw 'Provide both ArchivePath and ChecksumsPath.'
}
if ((Split-Path -Leaf $ArchivePath) -cne $name -or
    (Split-Path -Leaf $ChecksumsPath) -cne 'CHECKSUMS.sha256') {
    throw 'Asset filename differs from lock.'
}
Assert-Hash -Path $ArchivePath -Expected ([string]$lock.artifact.sha256)
Assert-Hash -Path $ChecksumsPath -Expected ([string]$lock.release.checksums.sha256)
$lines = @(Get-Content -LiteralPath $ChecksumsPath | Where-Object { $_ -ne '' })
if ($lines.Count -ne 1 -or $lines[0] -cne "$($lock.artifact.sha256) *$name") {
    throw 'Published checksum manifest differs from lock.'
}

Add-Type -AssemblyName System.IO.Compression
$archive = [IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $ArchivePath).Path)
try {
    $entryName = "$([IO.Path]::GetFileNameWithoutExtension($name))/share/tessera/package-metadata.json"
    $entry = $archive.GetEntry($entryName)
    if (-not $entry) { throw 'Tessera package metadata missing.' }
    $reader = [IO.StreamReader]::new($entry.Open())
    try { $metadata = $reader.ReadToEnd() | ConvertFrom-Json }
    finally { $reader.Dispose() }
    if ($metadata.source.revision -cne [string]$lock.sourceCommit -or
        $metadata.source.clean_at_configure -ne $true -or
        $metadata.scope.distribution_review -cne 'T034-local-verified') {
        throw 'Tessera source identity or review metadata differs from lock.'
    }
    foreach ($item in $archive.Entries) {
        if ($item.FullName -match '(^/|\.\.|\\|(^|/)(roms?|fixtures?|packs?)/|\.ay$|trust\.toml$|make_test_pack\.exe$)') {
            throw "Forbidden Tessera package entry: $($item.FullName)"
        }
    }
} finally { $archive.Dispose() }

$gh = Get-Command gh -ErrorAction SilentlyContinue
if (-not $gh) { throw 'GitHub CLI is required for attestation verification.' }
& $gh.Source attestation verify $ArchivePath `
    --repo $lock.attestation.repository `
    --signer-workflow $lock.attestation.signerWorkflow `
    --source-ref $lock.attestation.sourceRef `
    --predicate-type $lock.attestation.predicateType `
    --deny-self-hosted-runners | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'Tessera attestation verification failed.' }
Write-Host "Tessera package verified: $tag ($($lock.artifact.sha256))"