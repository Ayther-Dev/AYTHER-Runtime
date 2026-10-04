[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Bootstrap,
    [Parameter(Mandatory)][string]$QaLock,
    [Parameter(Mandatory)][string]$ReleaseLock,
    [Parameter(Mandatory)][string]$CacheDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-Condition {
    param(
        [Parameter(Mandatory)][bool]$Condition,
        [Parameter(Mandatory)][string]$Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

& $Bootstrap -LockFile $QaLock -ValidateOnly

$qa = Get-Content -LiteralPath $QaLock -Raw | ConvertFrom-Json
$release = Get-Content -LiteralPath $ReleaseLock -Raw | ConvertFrom-Json
$releaseEngine = @($release.artifacts | Where-Object {
    $_.platform -ceq 'windows' -and $_.architecture -ceq 'x86_64' -and
    $_.variant -ceq 'engine'
})
$releaseEngineVpx = @($release.artifacts | Where-Object {
    $_.platform -ceq 'windows' -and $_.architecture -ceq 'x86_64' -and
    $_.variant -ceq 'engine-vpx'
})
Assert-Condition ($releaseEngine.Count -eq 1) `
    'The release lock must contain one Windows engine artifact.'
Assert-Condition ($releaseEngineVpx.Count -eq 1) `
    'The release lock must contain one Windows engine-vpx artifact.'
# Spec 002, BR-174: the QA package is the published engine-vpx artifact of the locked release.
Assert-Condition ($qa.selection -ceq 'qa-release') `
    'The QA lock must select the published release channel.'
Assert-Condition ([bool]$qa.package.officialRelease) `
    'The QA package must be an official Engine release.'
Assert-Condition ($qa.artifact.variant -ceq 'engine-vpx') `
    'The final QA package must select the Windows VPX variant.'
Assert-Condition ($qa.release.tag -ceq $release.release.tag) `
    'The QA lock and the release lock must pin the same Engine release.'
Assert-Condition ($qa.artifact.sha256 -ceq $releaseEngineVpx[0].sha256) `
    'The QA package must be the Windows VPX artifact of the release lock.'

$first = @(& $Bootstrap -LockFile $QaLock -DestinationDirectory $CacheDirectory)
$second = @(& $Bootstrap -LockFile $QaLock -DestinationDirectory $CacheDirectory)
Assert-Condition ($first.Count -eq 1) `
    'A fresh QA bootstrap must return exactly one CMake prefix.'
Assert-Condition ($second.Count -eq 1) `
    'A cached QA bootstrap must return exactly one CMake prefix.'

$prefix = [IO.Path]::GetFullPath([string]$first[0])
Assert-Condition ($prefix -ceq [IO.Path]::GetFullPath([string]$second[0])) `
    'Fresh and cached QA bootstraps must select the same immutable prefix.'
Assert-Condition (Test-Path -LiteralPath $prefix -PathType Container) `
    'The QA bootstrap result must be an extracted package prefix.'

Assert-Condition (Test-Path -LiteralPath `
        (Join-Path $prefix 'include/ayther/engine/audio_observer.hpp') `
        -PathType Leaf) `
    'The extracted package must expose the audio observation contract.'
Assert-Condition (Test-Path -LiteralPath `
        (Join-Path $prefix 'include/vpx/vpx_decoder.h') -PathType Leaf) `
    'The extracted package must retain the locked VPX variant.'

Write-Host 'AYTHER Engine QA lock contract passed.'
