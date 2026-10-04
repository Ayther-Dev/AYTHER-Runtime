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

# Spec 002, BR-179: end users get the Runtime component only, as in beta.8; the QA tools ship in
# a separate archive with the QA Runtime they work with.
Assert-Condition ([regex]::Matches($workflowText, '-D CPACK_COMPONENTS_ALL=Runtime\b').Count -eq 2) `
    "The Windows and Linux packages must contain the Runtime component only."
Assert-Condition ($workflowText -match '(?m)^  package-windows-qa:\s*$') `
    "Windows QA package job is missing."
Assert-Condition ($workflowText -match '(?m)^    name: Release / Windows QA package\s*$') `
    "The Windows QA package job needs its stable name."
Assert-Condition ($workflowText -match 'tools/bootstrap_ayther_engine_qa\.ps1') `
    "The QA package must use the QA Engine bootstrap."
Assert-Condition ($workflowText -notmatch 'AllowLocalArtifactForTest') `
    "A release must never accept an unattested local Engine."
Assert-Condition ($workflowText -match 'cmake --preset windows-qa(\s|$)' -and
    $workflowText -match 'cmake --build --preset windows-qa(\s|$)' -and
    $workflowText -match 'ctest --preset windows-qa(\s|$)') `
    "The QA package must configure, build and test the windows-qa preset."
Assert-Condition ($workflowText -match 'CPACK_COMPONENTS_ALL="Runtime;qa"' -and
    $workflowText -match 'CPACK_ARCHIVE_COMPONENT_INSTALL=OFF') `
    "The QA package must hold the QA Runtime and the qa component in one archive."
Assert-Condition ($workflowText -match '--qa-capabilities' -and
    $workflowText -match 'options --format toml' -and
    $workflowText -match '--probe-core' -and
    $workflowText -match 'ayther_replay_qa\.exe') `
    "The QA package must smoke-test its Runtime and QA tools from the archive."
Assert-Condition ($workflowText -match 'SHA256SUMS-windows-qa') `
    "The QA package must publish its own checksums."
Assert-Condition ($workflowText -match '(?ms)^  publish:.*?needs:.*?- package-windows-qa') `
    "Publication must wait for the QA package."
Assert-Condition ($workflowText -match 'assets\.Count -ne 6') `
    "Publication must expect three archives and three checksum files."

$uses = [regex]::Matches($workflowText, '(?m)^\s*uses:\s+([^\s#]+)')
Assert-Condition ($uses.Count -eq 8) "Unexpected number of release actions."
foreach ($match in $uses) {
    Assert-Condition ($match.Groups[1].Value -match
        '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+@[0-9a-f]{40}$') `
        "Release action is not SHA-pinned: '$($match.Groups[1].Value)'."
}

$notes = Join-Path (Split-Path -Parent $projectPath) "docs/releases/v$version.md"
Assert-Condition (Test-Path -LiteralPath $notes -PathType Leaf) `
    "Release notes are missing: $notes"

Write-Host "Release workflow contract: OK ($version)"
