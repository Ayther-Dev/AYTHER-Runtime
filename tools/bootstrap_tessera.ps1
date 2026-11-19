<#
.SYNOPSIS
Download, verify and extract the locked AYTHER Tessera package; return its CMake prefix.

.DESCRIPTION
Reads dependencies/tessera.native-mvp.lock.json, downloads the Windows x86-64
ZIP and CHECKSUMS.sha256 from the locked release, verifies them with
tools/verify_tessera_native_mvp.ps1 (lock coordinates, SHA-256, checksum
manifest, package metadata, forbidden entries and SLSA provenance) and extracts
the archive. The last output line is the package prefix, to put on
CMAKE_PREFIX_PATH next to the native Engine package. Requires an authenticated
GitHub CLI for the attestation check.
#>
[CmdletBinding()]
param(
    [string]$LockFile = (Join-Path $PSScriptRoot '../dependencies/tessera.native-mvp.lock.json'),
    [string]$DestinationDirectory = (Join-Path $PSScriptRoot '../.deps/tessera')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$lock = Get-Content -LiteralPath $LockFile -Raw | ConvertFrom-Json
& (Join-Path $PSScriptRoot 'verify_tessera_native_mvp.ps1') -LockFile $LockFile -ValidateOnly |
    Out-Host

$name = [string]$lock.artifact.fileName
$root = [IO.Path]::GetFileNameWithoutExtension($name)
$destination = [IO.Path]::GetFullPath($DestinationDirectory)
$downloads = Join-Path $destination 'downloads'
$prefixes = Join-Path $destination 'prefixes'
$prefix = Join-Path $prefixes $root
New-Item -ItemType Directory -Force -Path $downloads, $prefixes | Out-Null

$archive = Join-Path $downloads $name
$checksums = Join-Path $downloads "$root.CHECKSUMS.sha256"
foreach ($asset in @(
        @{ Uri = [string]$lock.artifact.url; Path = $archive },
        @{ Uri = [string]$lock.release.checksums.url; Path = $checksums })) {
    Write-Host "Downloading $($asset.Uri)..."
    Invoke-WebRequest -Uri $asset.Uri -OutFile $asset.Path
}

# The verifier names the manifest by its published file name.
$manifestDirectory = Join-Path $downloads $root
New-Item -ItemType Directory -Force -Path $manifestDirectory | Out-Null
$manifest = Join-Path $manifestDirectory 'CHECKSUMS.sha256'
Copy-Item -LiteralPath $checksums -Destination $manifest -Force
& (Join-Path $PSScriptRoot 'verify_tessera_native_mvp.ps1') -LockFile $LockFile `
    -ArchivePath $archive -ChecksumsPath $manifest | Out-Host

if (Test-Path -LiteralPath $prefix) {
    Remove-Item -LiteralPath $prefix -Recurse -Force
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::ExtractToDirectory($archive, $prefixes)
if (-not (Test-Path -LiteralPath (Join-Path $prefix 'lib/cmake/Tessera/TesseraConfig.cmake'))) {
    throw "The Tessera archive did not extract to the expected prefix: $prefix"
}
Write-Host "  [ OK ] Tessera package extracted"
Write-Output $prefix
