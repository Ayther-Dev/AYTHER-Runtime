param(
    [Parameter(Mandatory)][string]$Checker,
    [Parameter(Mandatory)][string]$Runtime,
    [Parameter(Mandatory)][string]$Rom,
    [Parameter(Mandatory)][string]$Reference,
    [Parameter(Mandatory)][string]$Manifest,
    [Parameter(Mandatory)][string]$Pack,
    [Parameter(Mandatory)][string]$Trust,
    [Parameter(Mandatory)][string]$Take,
    [Parameter(Mandatory)][string]$OutputRoot
)
$ErrorActionPreference = 'Stop'
$expectedRuntime = [System.IO.Path]::GetFullPath($Runtime)
$launch = [System.Diagnostics.ProcessStartInfo]::new($Checker)
$launch.UseShellExecute = $false
$launch.CreateNoWindow = $true
$launch.RedirectStandardOutput = $true
$launch.RedirectStandardError = $true
foreach ($argument in @('check', '--runtime', $Runtime, '--rom', $Rom, '--reference', $Reference,
        '--play-manifest', $Manifest, '--pack', $Pack, '--trust-registry', $Trust,
        '--take', $Take, '--output', $OutputRoot, '--request-id', 'visible-cancellation',
        '--presentation', 'visible')) {
    $launch.ArgumentList.Add($argument)
}
$checkerProcess = [System.Diagnostics.Process]::Start($launch)
$stdout = $checkerProcess.StandardOutput.ReadToEndAsync()
$stderr = $checkerProcess.StandardError.ReadToEndAsync()
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    $closed = $false
    while (-not $checkerProcess.HasExited -and [DateTime]::UtcNow -lt $deadline) {
        $children = Get-CimInstance Win32_Process -Filter "ParentProcessId = $($checkerProcess.Id)"
        foreach ($child in $children) {
            if (-not $child.ExecutablePath -or
                [System.IO.Path]::GetFullPath($child.ExecutablePath) -ne $expectedRuntime) { continue }
            $runtimeProcess = Get-Process -Id $child.ProcessId -ErrorAction SilentlyContinue
            if ($runtimeProcess -and $runtimeProcess.MainWindowTitle -eq 'AYTHER Runtime - QA') {
                # Give the initialized window one second of actual replay before requesting close.
                Start-Sleep -Milliseconds 1000
                $closed = $runtimeProcess.CloseMainWindow()
                break
            }
        }
        if ($closed) { break }
        Start-Sleep -Milliseconds 20
    }
    if (-not $closed) { throw 'Owned replay window was not found for cancellation' }
    if (-not $checkerProcess.WaitForExit(15000)) { throw 'Cancelled replay did not terminate' }
    $report = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()
    if ($checkerProcess.ExitCode -ne 2 -or $report -notmatch 'diagnostic=replay_cancelled') {
        throw "Cancelled replay returned an incorrect outcome: $report"
    }
    $terminals = @(Get-ChildItem -LiteralPath (Join-Path $OutputRoot 'runs') -Recurse -Filter replay-result.toml)
    if ($terminals.Count -ne 1) { throw 'Cancellation terminal was not preserved' }
    $terminal = Get-Content -LiteralPath $terminals[0].FullName -Raw
    if ($terminal -notmatch 'cancelled = true' -or
        $terminal -notmatch 'inputs_consumed = ([0-9]+)') { throw 'Cancellation metadata is missing' }
    $consumed = [int]$Matches[1]
    if ($consumed -le 0 -or $consumed -ge 600) { throw 'Cancellation did not stop within the take' }
    $ledger = Get-Content -LiteralPath (Join-Path $OutputRoot 'request-ledger.toml') -Raw
    if ($ledger -notmatch "playback_result = 'cancelled'") { throw 'Cancellation was classified as natural end' }
    Write-Output "visible_cancellation_test: passed; consumed=$consumed; $report"
} finally {
    if (-not $checkerProcess.HasExited) { $checkerProcess.Kill($true); $checkerProcess.WaitForExit() }
    $checkerProcess.Dispose()
}
