[CmdletBinding()]
param(
    [string]$Workflow = (Join-Path $PSScriptRoot "../.github/workflows/security.yml"),
    [string]$Policy = (Join-Path $PSScriptRoot "../SECURITY.md")
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Assert-Condition {
    param(
        [Parameter(Mandatory)][bool]$Condition,
        [Parameter(Mandatory)][string]$Message
    )
    if (-not $Condition) { throw $Message }
}

$workflowText = Get-Content -LiteralPath $Workflow -Raw
$policyText = Get-Content -LiteralPath $Policy -Raw

Assert-Condition ($workflowText -match 'Security / Dependency Review') `
    "Dependency review check is missing."
Assert-Condition ($workflowText -match 'Security / Secret Scan') `
    "Secret scan check is missing."
Assert-Condition ($workflowText -match 'fail-on-severity:\s+high') `
    "High and critical dependency findings must block the PR."
Assert-Condition ($workflowText -match 'cancel-in-progress:\s+true') `
    "Obsolete security runs must be cancelled."
Assert-Condition ($workflowText -notmatch '(?m)^\s+[a-z-]+:\s+write\s*$') `
    "Security jobs must not receive write permissions."

$uses = [regex]::Matches($workflowText, '(?m)^\s*uses:\s+([^\s#]+)')
Assert-Condition ($uses.Count -eq 4) "Unexpected number of security actions."
foreach ($match in $uses) {
    Assert-Condition ($match.Groups[1].Value -match
        '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+@[0-9a-f]{40}$') `
        "Security action is not SHA-pinned: '$($match.Groups[1].Value)'."
}

foreach ($required in @('24 hours', 'seven days', 'expiry', 'rollback',
                         'never excepted')) {
    Assert-Condition ($policyText -match [regex]::Escape($required)) `
        "Security policy omits '$required'."
}

Write-Host "Security workflow contract: OK"
