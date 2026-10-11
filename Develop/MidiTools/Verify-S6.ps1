# SPDX-License-Identifier: AGPL-3.0-only
# One bounded batch: GUI displays including editors/tooltips estimated below 96.
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
Set-Location -LiteralPath $taskRoot
$taskRuntime = Join-Path $taskRoot 'build/release/bin'
$env:PATH = "$taskRuntime;C:/Qt/6.10.3/msvc2022_64/bin;$env:PATH"
$env:QT_QPA_PLATFORM = 'windows'
$env:QT_SCALE_FACTOR = '1'
function Invoke-Logged($Command, $Arguments, $Evidence) {
    & $Command @Arguments 2>&1 | Tee-Object -FilePath 'build.log'
    $buildExitCode = $LASTEXITCODE
    if ($buildExitCode -ne 0) {
        Get-Content -LiteralPath 'build.log'
        exit $buildExitCode
    }
    Copy-Item -LiteralPath 'build.log' -Destination $Evidence
}
Invoke-Logged 'pwsh' @('-NoProfile', '-File', "$PSScriptRoot/Prepare-Delivery.ps1") 'build/MidiTools-S6-deploy.log'
Invoke-Logged 'python' @("$PSScriptRoot/verify_delivery.py") 'build/MidiTools-S6-dependencies.log'
Invoke-Logged "$taskRuntime/midi_tools_delivery_tests.exe" @($taskRuntime) 'build/MidiTools-S6-delivery.log'
Invoke-Logged "$taskRuntime/midi_tools_foundation_tests.exe" @($taskRuntime) 'build/MidiTools-S6-foundation.log'
Invoke-Logged "$taskRuntime/midi_tools_synth_tests.exe" @("$PSScriptRoot/fixtures") 'build/MidiTools-S6-core.log'
foreach ($kind in @('split', 'matrix', 'synth_sidebar', 'synth_gui')) {
    Invoke-Logged "$taskRuntime/midi_tools_${kind}_tests.exe" @(
        $taskRuntime, $taskRoot, "$taskRoot/build/MidiTools-S6-real-window/$kind"
    ) "build/MidiTools-S6-$kind.log"
}
Write-Output 'PASS S6 offline regression and bounded Windows 100% real-window batch'
