# SPDX-License-Identifier: AGPL-3.0-only
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Set-Location $repositoryRoot
$runtimeRoot = Join-Path $repositoryRoot 'build/release/bin'
$buildRecord = Get-Content build/full-release-build.json -Raw | ConvertFrom-Json
$commit = & git rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify source revision.' }
if ($buildRecord.commit -ne $commit -or $buildRecord.configuration -ne 'Release' -or
    $buildRecord.target -ne 'ALL_BUILD' -or $buildRecord.vst2 -ne $false) {
    throw 'A successful full Release build of the current commit with VST2 disabled is required.'
}

$requiredPaths = @('WinJACKNexus.exe', 'Vst Scanner.exe', 'VSTProcess32.exe', 'VSTProcess64.exe',
                   'common.dll', 'audio.dll', 'node.dll', 'ui.dll', 'mod_patchbay.dll',
                   'control_deck.dll', 'wjn_default_theme.dll', 'libfluidsynth-3.dll', 'sfizz.dll',
                   'bass.dll', 'bassmix.dll', 'platforms/qwindows.dll', 'Qt6Core.dll',
                   'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6OpenGL.dll', 'Qt6OpenGLWidgets.dll',
                   'locate/manifest.json', 'locate/qt/qtbase_zh_CN.qm',
                   'locate/qt/qtbase_ja.qm', 'locate/qt/qtbase_ko.qm',
                   'nodes/vst/mod_vst_host.dll', 'prerequisites/jack2-win64-v1.9.22.exe',
                   'prerequisites/vc_redist.x64.exe', 'prerequisites/vc_redist.x86.exe')
foreach ($kind in @('mono', 'stereo', '5_1', '7_1', 'midi', 'sound', 'peak',
                    'gain_mono', 'gain_stereo', 'gain_5_1', 'gain_7_1')) {
    $requiredPaths += "nodes/signal_switch/$kind/wjn_signal_switch_$kind.dll"
}
foreach ($relative in $requiredPaths) {
    if (-not (Test-Path -LiteralPath (Join-Path $runtimeRoot $relative) -PathType Leaf)) {
        throw "Full Release runtime file missing: $relative"
    }
}

$licenseRoot = Join-Path $runtimeRoot 'licenses'
New-Item -ItemType Directory -Path $licenseRoot -Force | Out-Null
Copy-Item LICENSE (Join-Path $licenseRoot 'WinJACKNexus-AGPL-3.0.txt')
Copy-Item third_party/vst3sdk/LICENSE.txt (Join-Path $licenseRoot 'VST3-SDK.txt')
$midiLicenseRoot = Join-Path $licenseRoot 'midi-tools'
New-Item -ItemType Directory -Path $midiLicenseRoot -Force | Out-Null
Copy-Item Docs/MidiTools/DEPENDENCIES.md, src/midi_tools/Engines.cmake $midiLicenseRoot
foreach ($component in @(@{ name = 'fluidsynth'; path = 'build/_deps/fluidsynth-2.5.7' },
                          @{ name = 'sfizz'; path = 'build/_deps/sfizz-1.2.3' })) {
    $source = [IO.Path]::GetFullPath((Join-Path $repositoryRoot $component.path))
    foreach ($notice in Get-ChildItem -LiteralPath $source -File -Recurse |
             Where-Object { $_.Name -match '^(LICENSE|COPYING|NOTICE|AUTHORS|COPYRIGHT)(\..*)?$' }) {
        $relative = [IO.Path]::GetRelativePath($source, $notice.FullName)
        $destination = Join-Path $midiLicenseRoot (Join-Path $component.name $relative)
        New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
        Copy-Item -LiteralPath $notice.FullName -Destination $destination
    }
}
# Retain exact source archives and the application-side FluidSynth patch for rebuilding.
$sources = @(
    @{ url = 'https://github.com/FluidSynth/fluidsynth/archive/refs/tags/v2.5.7.tar.gz';
       file = 'fluidsynth-2.5.7.tar.gz'; hash = 'ce27840221ab00dd59bf27e85ecbba480c6c2a7c9fbec4243658f68f59c07f4a' },
    @{ url = 'https://github.com/sfztools/sfizz/releases/download/1.2.3/sfizz-1.2.3.tar.gz';
       file = 'sfizz-1.2.3.tar.gz'; hash = 'a9339eac7620d7f0f6b44bdfe860680fab73e66efad4b5f15b21198dd9436822' },
    @{ url = 'https://github.com/kthohr/gcem/archive/012ae73c6d0a2cb09ffe86475f5c6fba3926e200.zip';
       file = 'gcem.zip'; hash = '28159274c54e9640354852e172d10d88eb159f4e7f2fea42edbcd20105ed3526' }
)
foreach ($source in $sources) {
    $destination = Join-Path $midiLicenseRoot $source.file
    Invoke-WebRequest $source.url -OutFile $destination
    if ((Get-FileHash $destination -Algorithm SHA256).Hash -ne $source.hash) {
        throw "Source archive hash mismatch: $($source.file)"
    }
}
$qtLicenseRoot = Join-Path $licenseRoot 'Qt'
New-Item -ItemType Directory -Path $qtLicenseRoot -Force | Out-Null
foreach ($name in @('LGPL-3.0-only.txt', 'GPL-3.0-only.txt', 'Qt-GPL-exception-1.0.txt')) {
    Invoke-WebRequest "https://raw.githubusercontent.com/qt/qtbase/v6.10.3/LICENSES/$name" `
        -OutFile (Join-Path $qtLicenseRoot $name)
}
foreach ($name in @('README.md', 'README.en.md', 'README.ja.md', 'README.ko.md')) {
    Copy-Item -LiteralPath $name -Destination $runtimeRoot
}
$notice = @'
WinJACKNexus full Release for Windows x64 (VST3 enabled, VST2 disabled).
Install the bundled prerequisites first: JACK2 and Microsoft VC++ x64 AND x86.
Then run WinJACKNexus.exe. Keep every runtime subdirectory intact.
No sound banks, VST plug-ins or user sound effects are included.
Qt 6.10.3 sources/notices: https://github.com/qt/qtbase/tree/v6.10.3
JACK2 1.9.22 sources/notices: https://github.com/jackaudio/jack2/tree/v1.9.22
Application sources: https://github.com/AI-Hobbyist/WinJACKNexus
BASS/BASSmix retain the vendor terms in licenses/bass.txt and licenses/bassmix.txt.
This build does not claim physical device, live JACK or GUI acceptance on GitHub.
'@
[IO.File]::WriteAllText((Join-Path $runtimeRoot 'INSTALL.txt'), $notice, [Text.UTF8Encoding]::new($false))

$files = @(Get-ChildItem -LiteralPath $runtimeRoot -File -Recurse | Where-Object {
    $relative = [IO.Path]::GetRelativePath($runtimeRoot, $_.FullName).Replace('\', '/')
    $relative -notmatch '^examples/' -and
    $_.Extension -notin @('.pdb', '.lib', '.exp', '.log', '.py', '.ps1') -and
    $_.Name -notin @('AGENTS.md', '.gitkeep', 'release-manifest.json') -and
    $_.Name -notmatch '^(msvcp140|vcruntime140|concrt140|vcomp140).*\.dll$'
})
$manifest = [ordered]@{
    packageKind = 'full-release'
    commit = $commit
    configuration = 'Release'
    vst2 = $false
    vst3 = $true
    files = @($files | ForEach-Object {
        [ordered]@{ path = [IO.Path]::GetRelativePath($runtimeRoot, $_.FullName).Replace('\', '/')
                    bytes = $_.Length
                    sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
    })
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $runtimeRoot 'release-manifest.json') -Encoding utf8
$archivePath = Join-Path $repositoryRoot "build/WinJACKNexus-Release-x64-$($commit.Substring(0, 8)).zip"
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::Open($archivePath, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in $files) {
        $relative = [IO.Path]::GetRelativePath($runtimeRoot, $file.FullName).Replace('\', '/')
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, $relative) | Out-Null
    }
    [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive,
        (Join-Path $runtimeRoot 'release-manifest.json'), 'release-manifest.json') | Out-Null
}
finally { $archive.Dispose() }
$archive = [IO.Compression.ZipFile]::OpenRead($archivePath)
try {
    foreach ($file in $manifest.files) {
        $entry = $archive.GetEntry($file.path)
        if (-not $entry -or $entry.Length -ne $file.bytes) { throw "ZIP size mismatch: $($file.path)" }
        $stream = $entry.Open()
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $hash = [Convert]::ToHexString($sha.ComputeHash($stream)).ToLowerInvariant() }
        finally { $stream.Dispose(); $sha.Dispose() }
        if ($hash -ne $file.sha256) { throw "ZIP hash mismatch: $($file.path)" }
    }
}
finally { $archive.Dispose() }
$hash = (Get-FileHash $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $([IO.Path]::GetFileName($archivePath))" | Set-Content "$archivePath.sha256" -Encoding ascii
Write-Output "PACKAGE $archivePath"
Write-Output "PASS full Release payload: $($files.Count) files; ZIP sizes and SHA256 verified"
exit 0
