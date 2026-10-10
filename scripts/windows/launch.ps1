# Launch a locally built OpenLoco.exe against an existing Locomotion install.
# This only passes the install path on the command line. It does not copy or
# modify files in that folder, and it does not change %APPDATA%\OpenLoco.
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',
    [string]$LocomotionPath = 'D:\SteamLibrary\steamapps\common\Locomotion'
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'OpenLoco.Common.ps1')

if (-not (Test-LocomotionInstall -Path $LocomotionPath)) {
    Write-Error "Refusing to launch. Data\g1.DAT was not found under $LocomotionPath"
}

$exe = Get-OpenLocoExecutable -Configuration $Configuration
if (-not (Test-Path -LiteralPath $exe)) {
    Write-Error "Executable not found: $exe. Run scripts/windows/build.ps1 first."
}

$workDir = Split-Path -Parent $exe
Write-Output "Starting $exe"
Write-Output "Locomotion path (read-only): $LocomotionPath"
Start-Process -FilePath $exe -ArgumentList @('--locomotion_path', $LocomotionPath) -WorkingDirectory $workDir
