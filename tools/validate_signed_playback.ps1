[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Runtime,
    [Parameter(Mandatory)][string]$Core,
    [Parameter(Mandatory)][string]$CoreSha256,
    [Parameter(Mandatory)][string]$Rom,
    [Parameter(Mandatory)][string]$RomSha256,
    [Parameter(Mandatory)][string]$Pack,
    [Parameter(Mandatory)][string]$PackSha256,
    [Parameter(Mandatory)][string]$TrustRegistry,
    [Parameter(Mandatory)][string]$EvidenceDirectory,
    [switch]$RequireVp9
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $IsWindows -or [Runtime.InteropServices.RuntimeInformation]::OSArchitecture -ne 'X64') {
    throw 'This acceptance gate requires Windows x86-64.'
}
$inputs = @{}
foreach ($item in @(@('core', $Core, $CoreSha256), @('rom', $Rom, $RomSha256), @('pack', $Pack, $PackSha256))) {
    $actual = (Get-FileHash -LiteralPath $item[1] -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -cne $item[2].ToLowerInvariant()) { throw "$($item[0]) checksum mismatch" }
    $inputs[$item[0]] = @{ path = (Resolve-Path -LiteralPath $item[1]).Path; sha256 = $actual }
}
$runtimePath = (Resolve-Path -LiteralPath $Runtime).Path
$registryPath = (Resolve-Path -LiteralPath $TrustRegistry).Path
$evidence = [IO.Path]::GetFullPath($EvidenceDirectory)
New-Item -ItemType Directory -Force -Path $evidence | Out-Null
$start = [Diagnostics.ProcessStartInfo]::new($runtimePath)
$start.WorkingDirectory = $evidence
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
foreach ($argument in @('--core', $inputs.core.path, '--rom', $inputs.rom.path,
    '--pack', $inputs.pack.path, '--trust-registry', $registryPath,
    '--frames', '600', '--saves-dir', (Join-Path $evidence 'saves'), '--subsystems', '4294967295')) {
    $start.ArgumentList.Add($argument)
}
$process = [Diagnostics.Process]::new()
$process.StartInfo = $start
try {
    [void]$process.Start()
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $timedOut = -not $process.WaitForExit(60000)
    if ($timedOut) { $process.Kill($true); $process.WaitForExit() }
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    $stdout | Set-Content -LiteralPath (Join-Path $evidence 'stdout.log')
    $stderr | Set-Content -LiteralPath (Join-Path $evidence 'stderr.log')
    $videoFrames = 0
    if ($stdout -match 'video_decoded_frames=(\d+)') { $videoFrames = [int]$Matches[1] }
    $passed = -not $timedOut -and $process.ExitCode -eq 0 -and
        $stdout.Contains('has_pack=1') -and $stdout.Contains('--frames 600 reached') -and
        $stdout.Contains('Playback completed: has_pack=1') -and
        (-not $RequireVp9 -or $videoFrames -gt 0)
    $summary = @{
        passed = $passed; exit_code = $process.ExitCode; timeout = $timedOut
        frames_required = 600; video_decoded_frames = $videoFrames; vp9_required = [bool]$RequireVp9
        inputs = $inputs; runtime_sha256 = (Get-FileHash $runtimePath).Hash.ToLowerInvariant()
        registry_sha256 = (Get-FileHash $registryPath).Hash.ToLowerInvariant()
        utc = [DateTime]::UtcNow.ToString('o'); os = [Environment]::OSVersion.VersionString
    }
    $summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $evidence 'result.json')
    if (-not $passed) { throw "Signed playback failed. See $evidence" }
    Write-Host "PASS: has_pack=1, 600 frames, exit=0, video_decoded_frames=$videoFrames"
} finally {
    $process.Dispose()
}
