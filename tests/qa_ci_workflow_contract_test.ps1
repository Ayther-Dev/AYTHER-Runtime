<#
Spec 002, BR-175 prepared locally (RNF-8): the `windows-qa` job of pull request CI builds the
QA variant against the published QA Engine and runs the audio QA tests without GPU as a
mandatory check. Until BR-173 publishes the QA Engine and BR-174 pins it, the job refuses the
local development lock with an explicit message instead of building against a local path.
#>
[CmdletBinding()]
param(
    [string]$Workflow = (Join-Path $PSScriptRoot '../.github/workflows/qa.yml'),
    [string]$Presets = (Join-Path $PSScriptRoot '../CMakePresets.json')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

Assert-Condition (Test-Path -LiteralPath $Workflow -PathType Leaf) "QA workflow is missing: '$Workflow'."
$text = Get-Content -LiteralPath $Workflow -Raw
$presetDocument = Get-Content -LiteralPath $Presets -Raw | ConvertFrom-Json

Assert-Condition ($text -match '(?ms)^on:\s*\r?\n\s+pull_request:\s*\r?\n\s+branches:\s*\r?\n\s+- main\s*$') `
    'The QA job must run for pull requests targeting main.'
Assert-Condition ($text -match '(?m)^permissions:\s*\r?\n  attestations:\s+read\s*\r?\n  contents:\s+read\s*$') `
    'The QA workflow may only read contents and attestations.'
Assert-Condition ($text -notmatch '(?m)^\s+[a-z-]+:\s+write\s*$') 'The QA workflow must not grant write permissions.'
Assert-Condition ($text -match '(?m)^  windows-qa:\s*$') 'The workflow must define the windows-qa job.'
Assert-Condition ($text -match '(?m)^    name: Windows / QA\s*$') 'The job needs the stable check name "Windows / QA".'
Assert-Condition ($text -match 'runs-on:\s+windows-2025-vs2026') 'The QA job must use the Visual Studio 2026 runner.'

foreach ($match in [regex]::Matches($text, '(?m)^\s*uses:\s+([^\s#]+)')) {
    Assert-Condition ($match.Groups[1].Value -match '^actions/(checkout|upload-artifact)@[0-9a-f]{40}$') `
        "External action is not allowlisted and SHA-pinned: '$($match.Groups[1].Value)'."
}

Assert-Condition ($text -match "selection -cne 'qa-release'") `
    'The QA job must refuse a QA lock that is not a published release.'
Assert-Condition ($text -match 'BR-173') 'The refusal must name the publication it waits for (BR-173).'
Assert-Condition ($text -match 'tools/bootstrap_ayther_engine_qa\.ps1') 'The QA job must use the QA bootstrap.'
Assert-Condition ($text -notmatch 'AllowLocalArtifactForTest') 'CI must never accept an unattested local artifact.'
Assert-Condition ($text -match 'cmake --preset windows-qa(\s|$)') 'The QA job must configure the windows-qa preset.'
Assert-Condition ($text -match 'cmake --build --preset windows-qa(\s|$)') 'The QA job must build the windows-qa preset.'
Assert-Condition ($text -match 'ctest --preset windows-qa -L audio_qa -LE gpu') `
    'The QA job must run the audio QA tests without GPU.'
Assert-Condition ($text -match '--output-junit') 'The QA job must keep a JUnit report.'
Assert-Condition ($text -match 'if \(\$exitCode -ne 0\) \{ exit \$exitCode \}') 'A failing step must fail the job.'
Assert-Condition ($text -match '(?ms)if:\s*\$\{\{\s*always\(\)\s*\}\}.*?uses:\s+actions/upload-artifact@[0-9a-f]{40}') `
    'The QA job must always upload its logs.'

$qa = @($presetDocument.configurePresets | Where-Object name -ceq 'windows-qa')
Assert-Condition ($qa.Count -eq 1 -and $qa[0].cacheVariables.AYTHER_REQUIRE_AUDIO_QA_ENGINE_PACKAGE -ceq 'ON') `
    'The windows-qa preset must require the QA Engine package.'

Write-Host 'QA CI workflow contract: OK'
