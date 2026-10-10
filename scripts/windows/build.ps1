# Configure the documented Windows preset and build Release, Debug, or both.
# Safe to run repeatedly. Uses the vcpkg tag pinned in CMakeLists.txt.
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug', 'Both')]
    [string]$Configuration = 'Both',
    [switch]$SkipConfigure
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'OpenLoco.Common.ps1')

Set-OpenLocoBuildEnvironment
Set-Location -LiteralPath $script:RepoRoot

if (-not $SkipConfigure) {
    Write-Output "Configuring preset windows"
    cmake --preset windows
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

$presets = @()
if ($Configuration -eq 'Release' -or $Configuration -eq 'Both') {
    $presets += 'windows-release'
}
if ($Configuration -eq 'Debug' -or $Configuration -eq 'Both') {
    $presets += 'windows-debug'
}

foreach ($preset in $presets) {
    Write-Output "Building preset $preset"
    cmake --build --preset $preset
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}
