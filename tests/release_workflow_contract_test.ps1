[CmdletBinding()]
param(
    [string]$Workflow = (Join-Path $PSScriptRoot "../.github/workflows/release.yml"),
    [string]$Project = (Join-Path $PSScriptRoot "../CMakeLists.txt")
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
$projectPath = (Resolve-Path -LiteralPath $Project).Path
$projectText = Get-Content -LiteralPath $projectPath -Raw

Assert-Condition ($projectText -match
    'set\(AYTHER_RUNTIME_VERSION\s+"([0-9]+\.[0-9]+\.[0-9]+-beta\.[0-9]+)"\)') `
    "Project version is not a beta version."
$version = $Matches[1]

Assert-Condition ($workflowText.Contains(
    "'v[0-9]+.[0-9]+.[0-9]+-beta.[0-9]+'")) `
    "Release workflow does not restrict tags to beta versions."
Assert-Condition ($workflowText -match 'package-windows:') `
    "Windows package job is missing."
Assert-Condition ($workflowText -match 'package-linux:') `
    "Linux package job is missing."
Assert-Condition ($workflowText -match 'publish:') `
    "Publish job is missing."
Assert-Condition ($workflowText -match 'contents:\s+write') `
    "Publish job cannot create the GitHub release."
Assert-Condition ($workflowText -match 'gh release create') `
    "GitHub release creation command is missing."
Assert-Condition ($workflowText -match '--prerelease') `
    "Beta releases must be marked as prereleases."
Assert-Condition ($workflowText -match '--verify-tag') `
    "Release publication must verify the pushed tag."
Assert-Condition ($workflowText -match 'cpack\s+--config') `
    "Release workflow does not invoke CPack."
Assert-Condition ($workflowText -match 'ctest\s+--preset') `
    "Release workflow does not run CTest before packaging."
Assert-Condition ($workflowText -match 'SHA256SUMS') `
    "Release workflow does not publish checksums."
Assert-Condition ($workflowText -match
    'docs/releases/\$env:GITHUB_REF_NAME\.md') `
    "Release workflow does not consume tag-specific release notes."

$uses = [regex]::Matches($workflowText, '(?m)^\s*uses:\s+([^\s#]+)')
Assert-Condition ($uses.Count -eq 6) "Unexpected number of release actions."
foreach ($match in $uses) {
    Assert-Condition ($match.Groups[1].Value -match
        '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+@[0-9a-f]{40}$') `
        "Release action is not SHA-pinned: '$($match.Groups[1].Value)'."
}

$notes = Join-Path (Split-Path -Parent $projectPath) "docs/releases/v$version.md"
Assert-Condition (Test-Path -LiteralPath $notes -PathType Leaf) `
    "Release notes are missing: $notes"

Write-Host "Release workflow contract: OK ($version)"
