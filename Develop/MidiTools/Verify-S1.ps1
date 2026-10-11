# Foreground Release validation; no JACK server and no GUI in S1.
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
Set-Location -LiteralPath $taskRoot
$taskCmake = 'D:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
function Invoke-Logged($Command, $Arguments, $Evidence) {
    Clear-Content -LiteralPath 'build.log' -ErrorAction SilentlyContinue
    & $Command @Arguments 2>&1 | Tee-Object -FilePath 'build.log'
    $buildExitCode = $LASTEXITCODE
    if ($buildExitCode -ne 0) { Get-Content -LiteralPath 'build.log'; exit $buildExitCode }
    Copy-Item -LiteralPath 'build.log' -Destination $Evidence
}
Invoke-Logged $taskCmake @('-S','.','-B','build') 'build/MidiTools-S1-configure.log'
Invoke-Logged $taskCmake @('--build','build','--config','Release','--target','midi_tools_foundation_tests','midi_tools_engine_probe','--parallel','1') 'build/MidiTools-S1-build.log'
$taskCache = Get-Content -LiteralPath 'build/CMakeCache.txt'
$taskOutput = ($taskCache | Where-Object { $_ -match '^WJN_OUTPUT_DIR:PATH=' }) -replace '^WJN_OUTPUT_DIR:PATH=',''
if (-not $taskOutput) { throw 'Missing existing WJN_OUTPUT_DIR' }
$env:PATH = "$taskOutput;C:/Qt/6.10.3/msvc2022_64/bin;$env:PATH"
Invoke-Logged (Join-Path $taskOutput 'midi_tools_foundation_tests.exe') @($taskOutput) 'build/MidiTools-S1-foundation.log'
Invoke-Logged (Join-Path $taskOutput 'midi_tools_engine_probe.exe') @((Join-Path $taskRoot 'Develop/MidiTools/fixtures')) 'build/MidiTools-S1-engines.log'
