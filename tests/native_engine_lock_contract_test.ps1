param(
    [Parameter(Mandatory)][string]$FetchScript,
    [Parameter(Mandatory)][string]$ExistingLock,
    [Parameter(Mandatory)][string]$WorkDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

& $FetchScript -LockFile $ExistingLock -ValidateOnly | Out-Null

# The pinned native lock (Tessera route) must validate and be signed by the
# native release workflow for a native-* tag.
$pinned = Join-Path (Split-Path -Parent $ExistingLock) 'ayther-engine.native-mvp.lock.json'
& $FetchScript -LockFile $pinned -ValidateOnly | Out-Null
$native = Get-Content -LiteralPath $pinned -Raw | ConvertFrom-Json
if ($native.release.tag -notmatch '^native-v' -or
    $native.attestation.signerWorkflow -ne 'Ayther-Dev/AYTHER-Engine/.github/workflows/native-release.yml' -or
    $native.attestation.sourceRef -ne "refs/tags/$($native.release.tag)" -or
    @($native.artifacts).Count -ne 1) {
    throw "Pinned native Engine lock is not a single native-release artifact: $pinned"
}
$regular = Get-Content -LiteralPath $ExistingLock -Raw | ConvertFrom-Json
if ($native.release.tag -ne "native-$($regular.release.tag)") {
    throw "Native Engine lock $($native.release.tag) does not match the regular lock $($regular.release.tag)."
}

$lock = Get-Content -LiteralPath $ExistingLock -Raw | ConvertFrom-Json
$tag = 'native-v0.1.0-rc.21'
$base = 'https://github.com/Ayther-Dev/AYTHER-Engine/releases'
$assetName = "ayther-engine-vpx-$tag-windows-x86_64.zip"
$lock.release.tag = $tag
$lock.release.url = "$base/tag/$tag"
$lock.release.checksums.url = "$base/download/$tag/CHECKSUMS.sha256"
$lock.release.checksums.sha256 = '0' * 64
$lock.attestation.signerWorkflow = 'Ayther-Dev/AYTHER-Engine/.github/workflows/native-release.yml'
$lock.attestation.sourceRef = "refs/tags/$tag"
$lock.artifacts = @([pscustomobject]@{
    platform = 'windows'
    architecture = 'x86_64'
    variant = 'engine-vpx'
    vpx = $true
    fileName = $assetName
    url = "$base/download/$tag/$assetName"
    sha256 = '0' * 64
})

$directory = Join-Path $WorkDirectory ([guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($directory) | Out-Null
$valid = Join-Path $directory 'native-valid.json'
[IO.File]::WriteAllText($valid, ($lock | ConvertTo-Json -Depth 10),
    [Text.UTF8Encoding]::new($false))
& $FetchScript -LockFile $valid -ValidateOnly | Out-Null

$lock.attestation.signerWorkflow = 'Ayther-Dev/AYTHER-Engine/.github/workflows/release.yml'
$invalid = Join-Path $directory 'native-invalid-signer.json'
[IO.File]::WriteAllText($invalid, ($lock | ConvertTo-Json -Depth 10),
    [Text.UTF8Encoding]::new($false))
try {
    & $FetchScript -LockFile $invalid -ValidateOnly | Out-Null
    throw 'Wrong native signer was accepted.'
} catch {
    if ($_.Exception.Message -notmatch 'Unexpected attestation signer workflow') {
        throw
    }
}

Write-Host 'Engine lock contract accepts one native artifact and rejects the wrong signer.'
