# SPDX-License-Identifier: AGPL-3.0-only
# Call in the foreground through Tee-Object; no user data is copied or removed.
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$taskRuntime = Join-Path $taskRoot 'build/release/bin'
foreach ($kind in @('sf2_loader', 'midi_spilit', 'midi_matrix')) {
    $target = Join-Path $taskRuntime "nodes/midi_tools/$kind"
    New-Item -ItemType Directory -Path $target -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $taskRuntime "midi_tools/$kind/mod_$kind.dll") -Destination $target
    Copy-Item -LiteralPath (Join-Path $taskRuntime "midi_tools/$kind/locate") -Destination $target -Recurse -Force
    Write-Output "Deployed nodes/midi_tools/$kind and its catalogs"
}
$taskNotices = Join-Path $taskRuntime 'licenses/midi-tools'
New-Item -ItemType Directory -Path $taskNotices -Force | Out-Null
$taskSources = @{
    'fluidsynth' = 'build/_deps/fluidsynth-2.5.7'
    'sfizz' = 'build/_deps/sfizz-1.2.3'
    'gcem' = 'build/_deps/wjn_fluid-build/gcem-extracted/gcem-012ae73c6d0a2cb09ffe86475f5c6fba3926e200'
}
foreach ($entry in $taskSources.GetEnumerator()) {
    $source = Join-Path $taskRoot $entry.Value
    foreach ($file in Get-ChildItem -LiteralPath $source -Recurse -File) {
        if ($file.Name -notmatch '^(LICENSE|COPYING|NOTICE|COPYRIGHT|AUTHORS)(\.|$)') {
            continue
        }
        $relative = [IO.Path]::GetRelativePath($source, $file.FullName)
        $destination = Join-Path $taskNotices "$($entry.Key)/$relative"
        New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $destination
        Write-Output "Notice: $($entry.Key)/$relative"
    }
}
Copy-Item -LiteralPath (Join-Path $taskRoot 'Docs/MidiTools/DEPENDENCIES.md') -Destination $taskNotices
Copy-Item -LiteralPath (Join-Path $taskRoot 'src/midi_tools/Engines.cmake') -Destination $taskNotices
foreach ($entry in @{
        'fluidsynth-2.5.7.tar.gz' = 'build/_deps/wjn_fluid-subbuild/wjn_fluid-populate-prefix/src/v2.5.7.tar.gz'
        'sfizz-1.2.3.tar.gz' = 'build/_deps/wjn_sfizz-subbuild/wjn_sfizz-populate-prefix/src/sfizz-1.2.3.tar.gz'
        'gcem.zip' = 'build/_deps/wjn_fluid-build/gcem.zip'
    }.GetEnumerator()) {
    Copy-Item -LiteralPath (Join-Path $taskRoot $entry.Value) -Destination (Join-Path $taskNotices $entry.Key)
    Write-Output "Corresponding source: $($entry.Key)"
}
Write-Output 'PASS deployment and complete engine source/license materials; user files preserved'
