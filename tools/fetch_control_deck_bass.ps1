# SPDX-License-Identifier: AGPL-3.0-only
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$referenceRoot = Join-Path $repositoryRoot 'ref'
$runtimeRoot = Join-Path $repositoryRoot 'build/release/bin'
New-Item -ItemType Directory -Path $referenceRoot -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $runtimeRoot 'licenses') -Force | Out-Null
$packages = @(
    @{ name = 'bass'; hash = '3A03EC9A33D0F4F9D167660DA51C8BB1432E8977496995455AB137277D69636E' },
    @{ name = 'bassmix'; hash = '9E7FE1AF3122C0D78D07CB2788CD52AEF4E47B6C6F8AD0022AD5E40320516796' }
)
foreach ($package in $packages) {
    $name = $package.name
    $archive = Join-Path $referenceRoot ($name + '24.zip')
    if (-not (Test-Path -LiteralPath $archive)) {
        Invoke-WebRequest -Uri "https://www.un4seen.com/files/${name}24.zip" -OutFile $archive
    }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $package.hash) {
        throw "Official archive hash changed: $archive. Check the upstream release before updating the pin."
    }
    $sdkRoot = Join-Path $referenceRoot $name
    Expand-Archive -LiteralPath $archive -DestinationPath $sdkRoot -Force
    Copy-Item -LiteralPath (Join-Path $sdkRoot "x64/$name.dll") -Destination $runtimeRoot
    Copy-Item -LiteralPath (Join-Path $sdkRoot "$name.txt") -Destination (Join-Path $runtimeRoot "licenses/$name.txt")
    Write-Output "READY ref/$name and build/release/bin/$name.dll (x64)"
}
Write-Output 'PASS official archives verified; SDKs, examples and documentation are in ref'
