# SPDX-License-Identifier: AGPL-3.0-only
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$runtimeRoot = Join-Path $repositoryRoot 'build/release/bin'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$archivePath = Join-Path $repositoryRoot "build/WinJACKNexus-ControlDeck-incremental-$stamp.zip"

# This is an overlay for the existing x64 Qt 6.10.3 release, not a full release.
$runtimePaths = [Collections.Generic.List[string]]::new()
foreach ($name in @('control_deck.dll', 'ui.dll', 'mod_patchbay.dll', 'common.dll', 'bass.dll', 'bassmix.dll',
                     'licenses/bass.txt', 'licenses/bassmix.txt')) {
    $runtimePaths.Add($name)
}
$catalogFiles = @('manifest.json', 'en-US.json', 'zh-CN.json', 'ja-JP.json', 'ko-KR.json')
foreach ($name in $catalogFiles) {
    $runtimePaths.Add("signal_switch/locate/$name")
}
foreach ($kind in @('mono', 'stereo', '5_1', '7_1', 'midi', 'sound', 'peak',
                    'gain_mono', 'gain_stereo', 'gain_5_1', 'gain_7_1')) {
    $runtimePaths.Add("nodes/control_deck/$kind/wjn_signal_switch_$kind.dll")
    foreach ($name in $catalogFiles) {
        $runtimePaths.Add("nodes/control_deck/$kind/locate/$name")
    }
}

$files = foreach ($relative in $runtimePaths) {
    $absolute = Join-Path $runtimeRoot $relative
    if (-not (Test-Path -LiteralPath $absolute -PathType Leaf)) {
        throw "Required runtime file is missing: $relative"
    }
    $item = Get-Item -LiteralPath $absolute
    [ordered]@{
        path = $relative
        bytes = $item.Length
        sha256 = (Get-FileHash -LiteralPath $absolute -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}
$baseCommit = & git -C $repositoryRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot read source revision.' }
$manifest = [ordered]@{
    formatVersion = 1
    packageKind = 'incremental-overlay'
    component = 'Control Deck: switches, sound, mono peak meter and four gain knobs'
    created = (Get-Date).ToUniversalTime().ToString('o')
    sourceBaseCommit = $baseCommit
    sourceState = 'Built from the current working tree; base commit alone does not identify uncommitted sources.'
    prerequisites = @('Existing WinJACKNexus x64 Release', 'Qt 6.10.3 runtime', 'Existing Node v4 runtime')
    files = @($files)
}
$readme = @'
Control Deck incremental replacement package

Extract into the existing WinJACKNexus executable directory, preserving paths.
control_deck.dll belongs beside the EXE. The eleven node DLLs belong under
nodes/control_deck, each with its locate directory.
The main module keeps signal_switch/locate for compatibility. manifest.json lists SHA256 hashes.

This is not a full application distribution. Keep the existing EXE, Qt 6.10.3,
audio.dll, node.dll, patchbay_core.dll, default theme and other runtime files.
ui.dll and mod_patchbay.dll contain the integration; common.dll is included
because these binaries depend on the current workspace's shared-library ABI.
The build retains pre-existing workspace changes in those shared binaries.
No unrelated source changes are included in the Control Deck commit.

BASS and BASSmix are third-party runtime libraries with their own terms in
licenses/bass.txt and licenses/bassmix.txt. Their presence does not relicense
those libraries under this repository's source licence. SDKs are fetched from
the official vendor into ref by tools/fetch_control_deck_bass.ps1.
Extra compatible x64 BASS decoder plugins can be placed in signal_switch/bass_plugins.

Do not replace DLLs while the application is using them. Close the application
yourself when convenient, then extract and restart. The packaging process does
not close applications or interrupt audio.

All five signal switch versions default to enabled. Reopening a project
preserves saved enabled/disabled states. Presets are Base64 inside the project.
Lock layout preserves button positions and pins only the buttons above other
normal application windows. Open editor unlocks. With mouse click-through on,
hold Alt and left-click to operate a button without clicking the lower app.
Sound buttons trigger once; another trigger restarts playback. All loaded BASS
file formats are detected by content, including tracker music via BASS_MusicLoad.
Sound files remain external; their paths and shortcuts are saved in the project.
Mono peak meters have one input and no output. Gain knobs provide 1/2/6/8-channel versions,
with independent saved dB ranges; one knob controls all channels of its node.

Validation is offline plus visible Windows GUI at 100% scale. Live JACK chains,
physical monitor removal and real MIDI receiver cleanup are not verified.
Manual installation acceptance was skipped by user instruction.
'@

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::Open($archivePath, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($relative in $runtimePaths) {
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $archive, (Join-Path $runtimeRoot $relative), $relative,
            [IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
    $documents = @{
        'manifest.json' = ($manifest | ConvertTo-Json -Depth 8)
        'README-ControlDeck.txt' = $readme
    }
    foreach ($name in $documents.Keys) {
        $entry = $archive.CreateEntry($name)
        $writer = [IO.StreamWriter]::new($entry.Open(), [Text.UTF8Encoding]::new($false))
        try { $writer.Write($documents[$name]) }
        finally { $writer.Dispose() }
    }
}
finally { $archive.Dispose() }

# Verify the actual compressed payload, not only the pre-compression file list.
$archive = [IO.Compression.ZipFile]::OpenRead($archivePath)
try {
    if ($archive.Entries.Count -ne $runtimePaths.Count + 2) { throw 'Unexpected ZIP entry count.' }
    foreach ($file in $files) {
        $entry = $archive.GetEntry($file.path)
        if (-not $entry -or $entry.Length -ne $file.bytes) { throw "ZIP size mismatch: $($file.path)" }
        $stream = $entry.Open()
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $actual = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
        finally { $stream.Dispose(); $sha.Dispose() }
        if ($actual -ne $file.sha256) { throw "ZIP hash mismatch: $($file.path)" }
    }
}
finally { $archive.Dispose() }

Write-Output "PACKAGE $archivePath"
Write-Output "RUNTIME_FILES $($runtimePaths.Count)"
Write-Output "SHA256 $((Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash)"
Write-Output 'PASS ZIP paths, sizes and SHA256 verified'
