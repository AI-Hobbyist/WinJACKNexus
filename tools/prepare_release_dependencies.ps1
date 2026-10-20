# SPDX-License-Identifier: AGPL-3.0-only
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Set-Location $repositoryRoot
$dependencyRoot = Join-Path $repositoryRoot 'third_party'
$prerequisiteRoot = Join-Path $repositoryRoot 'build/release/bin/prerequisites'
New-Item -ItemType Directory -Path $dependencyRoot, $prerequisiteRoot -Force | Out-Null

function Invoke-Git {
    param([string[]] $Arguments)
    & git @Arguments
    if ($LASTEXITCODE -ne 0) { throw 'Dependency checkout failed.' }
}

# Match the SDK used by the local build. VST2 is explicitly disabled in CI.
$vstRoot = Join-Path $dependencyRoot 'vst3sdk'
if (-not (Test-Path -LiteralPath $vstRoot)) {
    Invoke-Git -Arguments @('clone', '--filter=blob:none', '--no-checkout',
                 'https://github.com/steinbergmedia/vst3sdk.git', $vstRoot)
}
Invoke-Git -Arguments @('-C', $vstRoot, 'checkout', '58f8da7936800732561402d7936584ca4505de07')
Invoke-Git -Arguments @('-C', $vstRoot, 'submodule', 'update', '--init', '--recursive',
             'base', 'cmake', 'pluginterfaces', 'public.sdk')

$jackRoot = Join-Path $dependencyRoot 'JACK2'
if (-not (Test-Path -LiteralPath $jackRoot)) {
    Invoke-Git -Arguments @('clone', '--depth', '1', '--branch', 'v1.9.22',
                 'https://github.com/jackaudio/jack2.git', $jackRoot)
}
New-Item -ItemType Directory -Path (Join-Path $jackRoot 'include') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $jackRoot 'common/jack') -Destination (Join-Path $jackRoot 'include') -Recurse -Force

$jackInstaller = Join-Path $prerequisiteRoot 'jack2-win64-v1.9.22.exe'
Invoke-WebRequest 'https://github.com/jackaudio/jack2-releases/releases/download/v1.9.22/jack2-win64-v1.9.22.exe' -OutFile $jackInstaller
if ((Get-FileHash $jackInstaller -Algorithm SHA256).Hash -ne
    'C46847404C3CBD01EA3473E53F7221078E34142308E911C0E08A40DC44D26A19') {
    throw 'Unexpected JACK2 installer hash.'
}

# Mixed-bitness helpers use the system CRT; do not put an x64 CRT beside x86 EXEs.
foreach ($architecture in @('x64', 'x86')) {
    $installer = Join-Path $prerequisiteRoot "vc_redist.$architecture.exe"
    Invoke-WebRequest "https://aka.ms/vs/17/release/vc_redist.$architecture.exe" -OutFile $installer
    $signature = Get-AuthenticodeSignature -LiteralPath $installer
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') {
        throw "Microsoft runtime signature verification failed: $architecture"
    }
}

& (Join-Path $PSScriptRoot 'fetch_control_deck_bass.ps1')
Write-Output 'PASS Release dependencies ready; VST2 SDK is not required'
exit 0
