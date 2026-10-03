param(
    [Parameter(Mandatory=$true)][ValidateSet('1','1.5','2')][string]$Scale,
    [Parameter(Mandatory=$true)][ValidateSet('Effects','Host','Community','Shared')][string]$Scope,
    [string[]]$Effects=@(),
    [ValidateSet('hot','en-US','zh-CN','ja-JP','ko-KR')][string]$Language='hot'
)
$ErrorActionPreference='Stop'
$repositoryRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
Set-Location -LiteralPath $repositoryRoot
$knownEffects=@('gate','graphic_eq','deesser','compressor','limiter','delay','parametric_eq','reverb')
if ($Scope -eq 'Effects') {
    if ($Effects.Count -eq 0 -or @($Effects | Where-Object { $_ -notin $knownEffects }).Count -gt 0) { throw 'Supply known effect IDs.' }
    if ($Language -eq 'hot' -and $Effects.Count -gt 2) { throw 'Hot switching is limited to two effects per batch; review each batch before continuing.' }
} elseif ($Effects.Count -gt 0) { throw 'Effects are valid only for the Effects scope.' }
if ($Scope -eq 'Shared' -and $Language -ne 'hot') { throw 'Shared fixtures explicitly exercise all four hot languages.' }
$runtime=Join-Path $repositoryRoot 'build/release/bin'
$testExecutable=Join-Path $runtime 'wjn_native_fx_i18n_tests.exe'
$stamp=[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
$scopeName=if ($Scope -eq 'Effects') { $Effects -join '_' } else { $Scope.ToLowerInvariant() }
$runId="$stamp-scale-$Scale-$scopeName-$Language"
$outputRelative="Docs/i18n/S5-bounded-rendered/$runId"
$logRelative="Docs/i18n/S5-bounded-records/$runId.log"
$recordRelative="Docs/i18n/S5-bounded-records/$runId.json"
New-Item -ItemType Directory -Force -Path (Join-Path $repositoryRoot 'Docs/i18n/S5-bounded-records') | Out-Null
$sourceFiles=@(
    'Develop/NativeFx/NativeFxI18nTests.cpp','Develop/I18nExample.hpp',
    'Develop/NativeFx/ExampleNativeFx.cpp','Develop/Node/ExampleNode.cpp',
    'src/native_fx/src/I18n.cpp','src/native_fx/src/NativeFxApi.cpp',
    'src/ui/src/Controls.cpp','src/ui/src/FxSettingsWindow.cpp',
    'src/ui/src/I18nManager.cpp','src/ui/src/HostI18n.cpp',
    'Docs/i18n/S5-strings.tsv','Docs/i18n/S5-shared-strings.tsv',
    'build/release/bin/wjn_native_fx_i18n_tests.exe','build/release/bin/ui.dll',
    'build/release/bin/examples/native_fx/wjn_example_native_fx.dll',
    'build/release/bin/examples/node/wjn_example_node.dll'
)
foreach ($effect in $knownEffects) {
    $sourceFiles+="src/native_fx/effects/$effect/src/Ui.cpp"
    $sourceFiles+="build/release/bin/nodes/native_fx/$effect/mod_$effect.dll"
}
$sourceFiles+=@(Get-ChildItem -LiteralPath (Join-Path $repositoryRoot 'locate') -File | ForEach-Object { 'locate/'+$_.Name })
foreach ($effect in $knownEffects) {
    $sourceFiles+=@(Get-ChildItem -LiteralPath (Join-Path $repositoryRoot "src/native_fx/effects/$effect/locate") -File | ForEach-Object { "src/native_fx/effects/$effect/locate/"+$_.Name })
}
$fingerprints=@($sourceFiles | ForEach-Object {
    @{file=$_;sha256=(Get-FileHash -LiteralPath (Join-Path $repositoryRoot $_) -Algorithm SHA256).Hash.ToLowerInvariant()}
})
$testArguments=@($runtime,(Join-Path $repositoryRoot $outputRelative))
switch ($Scope) {
    'Effects' { $testArguments+=@('--effects',($Effects -join ',')) }
    'Host' { $testArguments+='--host-only' }
    'Community' { $testArguments+='--community-only' }
    'Shared' { $testArguments+='--elision-only' }
}
if ($Language -ne 'hot') { $testArguments+=@('--cold',$Language) }
$env:QT_QPA_PLATFORM='windows'
$env:QT_SCALE_FACTOR=$Scale
$env:PATH='C:\Qt\6.10.3\msvc2022_64\bin;'+$env:PATH
Write-Host "One bounded S5 batch: $scopeName; scale=$Scale; language=$Language"
& $testExecutable @testArguments 2>&1 | Tee-Object -FilePath 'build.log'
$testExit=$LASTEXITCODE
Copy-Item -LiteralPath 'build.log' -Destination (Join-Path $repositoryRoot $logRelative)
if ($testExit -ne 0) { Get-Content -LiteralPath 'build.log' -Tail 50; exit $testExit }
$log=Get-Content -LiteralPath 'build.log' -Raw
$budget=[regex]::Match($log,'S5 popup budget: (\d+) / 100')
$pass=[regex]::Match($log,'S5 module Windows checks PASS: (\d+); platform=windows font=(.+?) scale=([\d.]+)')
if (!$budget.Success -or [int]$budget.Groups[1].Value -gt 100 -or !$pass.Success -or $pass.Groups[3].Value -ne $Scale) { throw 'Missing valid Windows PASS and popup budget evidence.' }
foreach ($fingerprint in $fingerprints) {
    if ((Get-FileHash -LiteralPath (Join-Path $repositoryRoot $fingerprint.file) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $fingerprint.sha256) { throw "Evidence changed during test: $($fingerprint.file)" }
}
$captures=@(Get-ChildItem -LiteralPath (Join-Path $repositoryRoot $outputRelative) -Filter '*.png' -File | ForEach-Object {
    @{file=($outputRelative+'/'+$_.Name);sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
})
if ($captures.Count -eq 0) { throw 'No native desktop captures.' }
$record=@{run_id=$runId;scale=$Scale;scope=$Scope;effects=@($Effects);language=$Language;
    exit_code=$testExit;popup_shows=[int]$budget.Groups[1].Value;checks=[int]$pass.Groups[1].Value;
    platform='windows';font=$pass.Groups[2].Value;log=$logRelative;
    log_sha256=(Get-FileHash -LiteralPath (Join-Path $repositoryRoot $logRelative) -Algorithm SHA256).Hash.ToLowerInvariant();
    rendered=$outputRelative;evidence=$fingerprints;captures=$captures}
[System.IO.File]::WriteAllText((Join-Path $repositoryRoot $recordRelative),($record | ConvertTo-Json -Depth 8)+[Environment]::NewLine,[System.Text.UTF8Encoding]::new($false))
Write-Host "Batch recorded: $recordRelative; popup shows=$($record.popup_shows) / 100"
exit 0
