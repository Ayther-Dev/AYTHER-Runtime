[CmdletBinding()]
param(
    [string]$Workflow = (Join-Path $PSScriptRoot "../.github/workflows/ci.yml"),
    [string]$Presets = (Join-Path $PSScriptRoot "../CMakePresets.json"),
    [string]$EngineLock = (Join-Path $PSScriptRoot "../dependencies/ayther-engine.lock.json")
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Assert-Condition {
    param(
        [Parameter(Mandatory)][bool]$Condition,
        [Parameter(Mandatory)][string]$Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

Assert-Condition (Test-Path -LiteralPath $Workflow -PathType Leaf) `
    "CI workflow is missing: '$Workflow'."
Assert-Condition (Test-Path -LiteralPath $Presets -PathType Leaf) `
    "CMake presets are missing: '$Presets'."

$lock = Get-Content -LiteralPath $EngineLock -Raw | ConvertFrom-Json
Assert-Condition ($lock.release.tag -ceq "v0.1.0-rc.17") `
    "RF-18 requires the Engine v0.1.0-rc.17 lock."

$workflowText = Get-Content -LiteralPath $Workflow -Raw
$presetDocument = Get-Content -LiteralPath $Presets -Raw | ConvertFrom-Json

Assert-Condition ($workflowText -match '(?m)^on:\s*$') `
    "Workflow must declare its event map explicitly."
Assert-Condition ($workflowText -match '(?ms)^on:\s*\r?\n\s+pull_request:\s*\r?\n\s+branches:\s*\r?\n\s+- main\s*$') `
    "Workflow must run for pull requests targeting main."
Assert-Condition ($workflowText -match '(?m)^permissions:\s*\r?\n  attestations:\s+read\s*\r?\n  contents:\s+read\s*\r?\n\s*\r?\n') `
    "Workflow permissions must be limited to attestations: read and contents: read."
Assert-Condition ($workflowText -notmatch '(?m)^[ \t]+permissions:\s*$') `
    "Jobs and steps must not broaden the workflow-level permissions."
Assert-Condition ($workflowText -notmatch '(?m)^\s+[a-z-]+:\s+write\s*$') `
    "Workflow must not grant write permissions."

Assert-Condition ($workflowText -match '(?m)^  windows:\s*$') `
    "Workflow must define a Windows job."
Assert-Condition ($workflowText -match '(?m)^  linux:\s*$') `
    "Workflow must define a Linux job."
Assert-Condition ($workflowText -match 'runs-on:\s+windows-2025-vs2026') `
    "Windows CI must use the Visual Studio 2026 runner image."
Assert-Condition ($workflowText -match 'runs-on:\s+ubuntu-24\.04') `
    "Linux CI must use the pinned Ubuntu 24.04 runner image."

$uses = [regex]::Matches($workflowText, '(?m)^\s*uses:\s+([^\s#]+)')
Assert-Condition ($uses.Count -gt 0) "Workflow must use pinned actions."
foreach ($match in $uses) {
    $action = $match.Groups[1].Value
    Assert-Condition ($action -match '^actions/(checkout|upload-artifact)@[0-9a-f]{40}$') `
        "External action is not allowlisted and SHA-pinned: '$action'."
}

Assert-Condition ($workflowText -match 'tools/bootstrap_ayther_engine\.ps1') `
    "CI must use Runtime's locked Engine bootstrap."
Assert-Condition ($workflowText -match 'v0\.1\.0-rc\.17') `
    "CI must reject any Engine lock other than v0.1.0-rc.17."
Assert-Condition ($workflowText -match 'cmake --preset') `
    "CI configuration must use CMake presets."
Assert-Condition ($workflowText -match 'cmake --build --preset') `
    "CI builds must use CMake build presets."
$incrementalSteps = [regex]::Matches(
    $workflowText,
    '(?m)^\s+- name: Verify build is up to date\s*$')
Assert-Condition ($incrementalSteps.Count -eq 2) `
    "Both platform jobs must verify that a second build has no pending work."
$buildInvocations = [regex]::Matches($workflowText, 'cmake --build --preset')
Assert-Condition ($buildInvocations.Count -eq 4) `
    "Each platform must run one full build and one incremental no-op build."
Assert-Condition ($workflowText -match 'ctest --preset') `
    "CI tests must use CTest presets."
Assert-Condition ($workflowText -match '--output-junit') `
    "CTest must produce JUnit output."
$testSteps = [regex]::Matches(
    $workflowText,
    '(?ms)^\s+- name: Test\s*$.*?(?=^\s+- name:|\z)')
Assert-Condition ($testSteps.Count -eq 2) `
    "Both platform jobs must execute their complete configured CTest suite."
foreach ($testStep in $testSteps) {
    Assert-Condition ($testStep.Value -match 'ctest --preset') `
        "Every Test step must execute the configured CTest preset."
    Assert-Condition ($testStep.Value -match 'if \(\$exitCode -ne 0\) \{ exit \$exitCode \}') `
        "Every Test step must propagate a failing CTest exit code."
    Assert-Condition ($testStep.Value -notmatch '(?m)\s-(E|LE|R|L)\s') `
        "The mandatory Test step must not filter out configured QA families."
}
$familyDiscoverySteps = [regex]::Matches(
    $workflowText,
    '(?m)^\s+- name: Verify required CTest families\s*$')
Assert-Condition ($familyDiscoverySteps.Count -eq 2) `
    "Both platform jobs must reject missing required CTest families."
Assert-Condition ($workflowText -match 'tools/check_ctest_families\.cmake') `
    "CI must use the shared CTest family discovery check."
$requiredFamilies = `
    'audio_qa,unit,contract,integration,limits,protocol,fixture'
$requiredFamilyInvocations = [regex]::Matches(
    $workflowText,
    [regex]::Escape("-DQA_REQUIRED_FAMILIES=$requiredFamilies"))
Assert-Condition ($requiredFamilyInvocations.Count -eq 2) `
    "Both platform jobs must discover every required audio QA family."
$formatSteps = [regex]::Matches(
    $workflowText,
    '(?m)^\s+- name: Check modified C\+\+ formatting\s*$')
Assert-Condition ($formatSteps.Count -eq 1) `
    "Linux quality CI must block on modified C++ formatting."
Assert-Condition ($workflowText -match 'tools/run_clang_format_changed\.sh') `
    "CI must invoke the checked-in clang-format gate."
$fullHistoryCheckouts = [regex]::Matches(
    $workflowText,
    '(?m)^\s+fetch-depth:\s+0\s*$')
Assert-Condition ($fullHistoryCheckouts.Count -eq 2) `
    "Both platform jobs must fetch enough history for change-based gates."

$runtimeRoot = Split-Path -Parent $PSScriptRoot
$runtimeCmake = Get-Content -LiteralPath (Join-Path $runtimeRoot 'CMakeLists.txt') -Raw
$audioQaCmake = Get-Content -LiteralPath `
    (Join-Path $runtimeRoot 'tests/audio_qa/CMakeLists.txt') -Raw
$audioQaToolCmake = Get-Content -LiteralPath `
    (Join-Path $runtimeRoot 'tools/audio_qa/CMakeLists.txt') -Raw
$mandatoryGraph = $runtimeCmake + "`n" + $audioQaCmake + "`n" + $audioQaToolCmake
foreach ($token in @(
        'add_executable(ayther_audio_qa',
        'audio_qa_engine_package_contract',
        'audio_qa_real_replay_integration',
        'audio_qa_runtime_process')) {
    Assert-Condition ($mandatoryGraph.Contains($token)) `
        "The mandatory Runtime build/test graph omits '$token'."
}

foreach ($artifact in @(
        'configure.log',
        'build.log',
        'incremental-build.log',
        'LastTest.log',
        'junit.xml')) {
    Assert-Condition ($workflowText.Contains($artifact)) `
        "Workflow must retain '$artifact'."
}
$alwaysUploads = [regex]::Matches(
    $workflowText,
    '(?ms)if:\s*\$\{\{\s*always\(\)\s*\}\}.*?uses:\s+actions/upload-artifact@[0-9a-f]{40}')
Assert-Condition ($alwaysUploads.Count -eq 2) `
    "Both platform jobs must always upload their logs."

foreach ($name in @('windows-ci', 'linux-ci')) {
    Assert-Condition (@($presetDocument.configurePresets | Where-Object name -ceq $name).Count -eq 1) `
        "Missing unique configure preset '$name'."
    Assert-Condition (@($presetDocument.buildPresets | Where-Object name -ceq $name).Count -eq 1) `
        "Missing unique build preset '$name'."
    Assert-Condition (@($presetDocument.testPresets | Where-Object name -ceq $name).Count -eq 1) `
        "Missing unique test preset '$name'."
}

$windows = @($presetDocument.configurePresets | Where-Object name -ceq 'windows-ci')[0]
$linux = @($presetDocument.configurePresets | Where-Object name -ceq 'linux-ci')[0]
$base = @($presetDocument.configurePresets | Where-Object name -ceq 'ci-base')
Assert-Condition ($base.Count -eq 1 -and $base[0].hidden) `
    "CI configure presets must share one hidden base."
Assert-Condition ($windows.generator -ceq 'Visual Studio 18 2026') `
    "Windows CI must select Visual Studio 2026 explicitly."
Assert-Condition ($windows.toolset -ceq 'v145') `
    "Windows CI must select the Engine-compatible v145 toolset."
Assert-Condition ($linux.generator -ceq 'Ninja') `
    "Linux CI must use Ninja."
Assert-Condition ($linux.cacheVariables.CMAKE_BUILD_TYPE -ceq 'RelWithDebInfo') `
    "Linux CI must build RelWithDebInfo."

foreach ($preset in @($windows, $linux)) {
    Assert-Condition ($preset.inherits -ceq 'ci-base') `
        "Preset '$($preset.name)' must inherit the locked CI base."
}
Assert-Condition ($base[0].cacheVariables.CMAKE_PREFIX_PATH -ceq '$env{AYTHER_ENGINE_PREFIX}') `
    "CI presets must consume only the bootstrapped Engine prefix."
Assert-Condition ($base[0].cacheVariables.CMAKE_TOOLCHAIN_FILE -ceq '$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake') `
    "CI presets must use the runner's vcpkg toolchain."

Write-Host "CI workflow contract: OK"
