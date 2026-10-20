# SPDX-License-Identifier: AGPL-3.0-only
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$fixtureRoot = Join-Path $repositoryRoot 'build/ControlDeck-fixtures'
New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null
& ffmpeg -y -f lavfi -i 'sine=frequency=440:duration=0.25' -ac 2 -ar 48000 -c:a pcm_f32le (Join-Path $fixtureRoot 'tone.wav')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
foreach ($format in @('aiff', 'mp3', 'ogg')) {
    & ffmpeg -y -i (Join-Path $fixtureRoot 'tone.wav') (Join-Path $fixtureRoot "tone.$format")
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
# A self-authored ProTracker module, with one looping sine sample and one note.
$moduleBytes = [byte[]]::new(1084 + 1024 + 32)
[Text.Encoding]::ASCII.GetBytes('Control Deck fixture').CopyTo($moduleBytes, 0)
$moduleBytes[43] = 16
$moduleBytes[45] = 64
$moduleBytes[49] = 16
$moduleBytes[950] = 1
[Text.Encoding]::ASCII.GetBytes('M.K.').CopyTo($moduleBytes, 1080)
$moduleBytes[1084] = 1
$moduleBytes[1085] = 172
$moduleBytes[1086] = 16
for ($index = 0; $index -lt 32; $index++) {
    $sample = [int](64 * [Math]::Sin(2 * [Math]::PI * $index / 32))
    $moduleBytes[2108 + $index] = [byte](($sample + 256) % 256)
}
[IO.File]::WriteAllBytes((Join-Path $fixtureRoot 'tone.mod'), $moduleBytes)
Write-Output 'PASS generated WAV/AIFF/MP3/OGG and original MOD fixtures under the existing build directory'
