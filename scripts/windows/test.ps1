# Run the CTest suite for the Visual Studio multi-config build.
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'OpenLoco.Common.ps1')

if (-not (Test-Path -LiteralPath $script:BuildDir)) {
    Write-Error "Build directory not found: $script:BuildDir. Run scripts/windows/build.ps1 first."
}

Set-Location -LiteralPath $script:RepoRoot
ctest --test-dir $script:BuildDir -C $Configuration --output-on-failure --parallel
exit $LASTEXITCODE
