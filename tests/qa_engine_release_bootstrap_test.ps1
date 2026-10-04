<#
Spec 002, BR-174 (RNF-8): the QA bootstrap downloads and verifies the published Engine release.
A `qa-release` lock names the `engine-vpx` artifact of an Engine release by URL, with its SHA-256,
the release CHECKSUMS.sha256 and its provenance attestation; it never points to a local path.
The test builds a small package with the layout the bootstrap requires and serves it by file URL,
which the bootstrap accepts only with -AllowLocalArtifactForTest, because a local file has no
attestation to verify.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Bootstrap,
    [Parameter(Mandatory)][string]$WorkDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Assert-Fails([scriptblock]$Action, [string]$Pattern, [string]$Message) {
    try {
        & $Action | Out-Null
    } catch {
        if ($_.Exception.Message -match $Pattern) { return }
        throw "$Message (unexpected error: $($_.Exception.Message))"
    }
    throw "$Message (no error)"
}

function Get-Sha256([string]$Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

Remove-Item -LiteralPath $WorkDirectory -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $WorkDirectory | Out-Null

# A published release in miniature: the engine-vpx archive and the release CHECKSUMS.sha256.
$tag = 'v0.1.0-rc.99'
$root = "ayther-engine-vpx-$tag-windows-x86_64"
$release = Join-Path $WorkDirectory 'release'
$package = Join-Path $WorkDirectory "package/$root"
foreach ($file in @(
    'include/ayther/engine/audio_observer.hpp',
    'include/ayther/engine/audio_production_limit.hpp',
    'include/ayther/engine/render_observer.hpp',
    'include/ayther/engine/visual_state.hpp',
    'include/vpx/vpx_decoder.h',
    'lib/cmake/Ayther/AytherConfig.cmake',
    'lib/cmake/Ayther/AytherEngineTargets.cmake'
)) {
    $path = Join-Path $package $file
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path) | Out-Null
    Set-Content -LiteralPath $path -Value "// $file" -Encoding utf8NoBOM
}
New-Item -ItemType Directory -Force -Path $release | Out-Null
$archive = Join-Path $release "$root.zip"
Compress-Archive -Path (Join-Path $WorkDirectory 'package/*') -DestinationPath $archive
$archiveHash = Get-Sha256 $archive
$checksums = Join-Path $release 'CHECKSUMS.sha256'
"$archiveHash *$root.zip" | Set-Content -LiteralPath $checksums -Encoding utf8NoBOM
$checksumsHash = Get-Sha256 $checksums

function New-ReleaseLock([string]$Path, [hashtable]$Changes = @{}) {
    $lock = [ordered]@{
        schemaVersion = 2
        dependency = 'AYTHER Engine'
        selection = 'qa-release'
        package = [ordered]@{
            id = $root; kind = 'release'; officialRelease = $true; productVersion = '0.1.0'
            audioObservationContract = '1.0'
        }
        release = [ordered]@{
            tag = $tag
            checksumsUrl = ([Uri]$checksums).AbsoluteUri
            checksumsSha256 = $checksumsHash
        }
        artifact = [ordered]@{
            platform = 'windows'; architecture = 'x86_64'; variant = 'engine-vpx'
            url = ([Uri]$archive).AbsoluteUri
            archiveRoot = $root; sha256 = $archiveHash
        }
        attestation = [ordered]@{
            repository = 'Ayther-Dev/AYTHER-Engine'
            signerWorkflow = 'Ayther-Dev/AYTHER-Engine/.github/workflows/release.yml'
            sourceRef = "refs/tags/$tag"
            predicateType = 'https://slsa.dev/provenance/v1'
        }
    }
    foreach ($key in $Changes.Keys) {
        $section, $field = $key.Split('.')
        if ($null -eq $Changes[$key]) { $lock[$section].Remove($field) }
        else { $lock[$section][$field] = $Changes[$key] }
    }
    $lock | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $Path -Encoding utf8NoBOM
    return $Path
}

$published = "https://github.com/Ayther-Dev/AYTHER-Engine/releases/download/$tag"

# 1. The published artifact is fetched, checked against the lock and the release checksums,
#    and extracted; a second run reuses the verified cache.
$lock = New-ReleaseLock (Join-Path $WorkDirectory 'release.lock.json')
$destination = Join-Path $WorkDirectory 'deps'
$prefix = & $Bootstrap -LockFile $lock -DestinationDirectory $destination -AllowLocalArtifactForTest |
    Select-Object -Last 1
Assert-Condition (Test-Path -LiteralPath (Join-Path $prefix 'include/ayther/engine/render_observer.hpp')) `
    "RNF-8: the verified QA artifact was not extracted: '$prefix'."
$again = & $Bootstrap -LockFile $lock -DestinationDirectory $destination -AllowLocalArtifactForTest |
    Select-Object -Last 1
Assert-Condition ($again -eq $prefix) 'the verified cache is reused'

# 2. A file changed in the cache after extraction is refused.
Add-Content -LiteralPath (Join-Path $prefix 'include/vpx/vpx_decoder.h') -Value '// changed'
Assert-Fails { & $Bootstrap -LockFile $lock -DestinationDirectory $destination -AllowLocalArtifactForTest } `
    'checksum mismatch' 'RNF-8: a changed cached prefix must be refused'

# 3. A local file has no attestation: without the test switch it is refused.
Assert-Fails { & $Bootstrap -LockFile $lock -DestinationDirectory (Join-Path $WorkDirectory 'deps2') } `
    'attestation' 'RNF-8: an unattested local artifact must be refused'

# 4. A different SHA-256 is refused before extracting anything.
$tampered = New-ReleaseLock (Join-Path $WorkDirectory 'tampered.lock.json') @{
    'artifact.sha256' = ('0' * 64)
}
Assert-Fails { & $Bootstrap -LockFile $tampered -DestinationDirectory (Join-Path $WorkDirectory 'deps3') -AllowLocalArtifactForTest } `
    'checksum mismatch' 'RNF-8: a changed artifact must be refused'
Assert-Condition (-not (Test-Path -LiteralPath (Join-Path $WorkDirectory "deps3/$('0' * 64)"))) `
    'nothing is extracted from a refused artifact'

# 5. The release checksums must publish the locked SHA-256.
$otherChecksums = Join-Path $release 'OTHER.sha256'
"$('1' * 64) *$root.zip" | Set-Content -LiteralPath $otherChecksums -Encoding utf8NoBOM
$unpublished = New-ReleaseLock (Join-Path $WorkDirectory 'unpublished.lock.json') @{
    'release.checksumsUrl' = ([Uri]$otherChecksums).AbsoluteUri
    'release.checksumsSha256' = (Get-Sha256 $otherChecksums)
}
Assert-Fails { & $Bootstrap -LockFile $unpublished -DestinationDirectory (Join-Path $WorkDirectory 'deps4') -AllowLocalArtifactForTest } `
    'published' 'RNF-8: an artifact the release checksums do not publish must be refused'

# 6. A release lock never points to a local path.
$local = New-ReleaseLock (Join-Path $WorkDirectory 'local.lock.json') @{
    'artifact.relativePath' = '../../AYTHER-Engine-002/dist/replay-qa-002/x.zip'
}
Assert-Fails { & $Bootstrap -LockFile $local -ValidateOnly } 'local path' `
    'RNF-8: a release lock with a local path must be refused'

# 7. Without attestation coordinates a remote artifact is refused.
$unattested = New-ReleaseLock (Join-Path $WorkDirectory 'unattested.lock.json') @{
    'artifact.url' = "$published/$root.zip"
    'release.checksumsUrl' = "$published/CHECKSUMS.sha256"
    'attestation.repository' = $null
}
Assert-Fails { & $Bootstrap -LockFile $unattested -ValidateOnly } 'attestation' `
    'RNF-8: a published artifact without its provenance must be refused'

# 8. A remote artifact must be the asset of the locked release.
$elsewhere = New-ReleaseLock (Join-Path $WorkDirectory 'elsewhere.lock.json') @{
    'artifact.url' = "https://github.com/Ayther-Dev/AYTHER-Engine/releases/download/v0.1.0-rc.1/$root.zip"
    'release.checksumsUrl' = "$published/CHECKSUMS.sha256"
}
Assert-Fails { & $Bootstrap -LockFile $elsewhere -ValidateOnly } 'release asset' `
    'RNF-8: an artifact of another release must be refused'

# 9. A relative cache directory belongs to the repository, not to the caller's working directory.
$repository = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $Bootstrap) '..'))
$relative = "out/qa-bootstrap-relative-$([guid]::NewGuid().ToString('N'))"
$previous = [Environment]::CurrentDirectory
try {
    [Environment]::CurrentDirectory = $WorkDirectory
    Push-Location -LiteralPath $WorkDirectory
    $placed = & $Bootstrap -LockFile $lock -DestinationDirectory $relative -AllowLocalArtifactForTest |
        Select-Object -Last 1
} finally {
    Pop-Location
    [Environment]::CurrentDirectory = $previous
}
$expectedRoot = [IO.Path]::GetFullPath((Join-Path $repository $relative))
Assert-Condition ([IO.Path]::GetFullPath([string]$placed).StartsWith($expectedRoot, [StringComparison]::OrdinalIgnoreCase)) `
    "a relative cache directory resolves against the repository: '$placed', expected under '$expectedRoot'"
Assert-Condition (-not (Test-Path -LiteralPath (Join-Path $WorkDirectory $relative))) `
    'nothing is written under the caller working directory'
Remove-Item -LiteralPath $expectedRoot -Recurse -Force -ErrorAction SilentlyContinue

# 10. A complete remote lock validates offline, without downloading.
$remote = New-ReleaseLock (Join-Path $WorkDirectory 'remote.lock.json') @{
    'artifact.url' = "$published/$root.zip"
    'release.checksumsUrl' = "$published/CHECKSUMS.sha256"
}
& $Bootstrap -LockFile $remote -ValidateOnly | Out-Null

Write-Host 'qa_engine_release_bootstrap: the QA bootstrap verifies a published Engine release'
