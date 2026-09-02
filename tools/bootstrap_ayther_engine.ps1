<#
.SYNOPSIS
Bootstrap the verified AYTHER Engine package and return its CMake prefix.

.DESCRIPTION
Stable entry point for Runtime dependency setup. Selection, download, checksum
and SLSA verification, extraction, cache validation, and prefix output are
implemented by fetch_ayther_engine.ps1 so the lock has one enforcement path.
#>
[CmdletBinding()]
param(
    [string]$LockFile,

    [ValidateSet("windows", "linux")]
    [string]$Platform,

    [ValidateSet("x86_64")]
    [string]$Architecture,

    [ValidateSet("engine", "engine-vpx")]
    [string]$Variant,

    [string]$DestinationDirectory,

    [string]$ArchivePath,

    [switch]$ValidateOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

& (Join-Path $PSScriptRoot "fetch_ayther_engine.ps1") @PSBoundParameters
