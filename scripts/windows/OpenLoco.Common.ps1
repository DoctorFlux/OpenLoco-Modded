# Shared paths for the local Windows OpenLoco workflow.
# These scripts never write into the original Locomotion installation.

$script:RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$script:DefaultLocomotionPath = 'D:\SteamLibrary\steamapps\common\Locomotion'
$script:VcpkgBinaryCache = Join-Path $script:RepoRoot 'build\vcpkg-cache'
$script:BuildDir = Join-Path $script:RepoRoot 'build\windows'
$script:VsInstall = 'C:\Program Files\Microsoft Visual Studio\2022\Community'

function Set-OpenLocoBuildEnvironment {
    if (-not (Test-Path -LiteralPath $script:VcpkgBinaryCache)) {
        New-Item -ItemType Directory -Force -Path $script:VcpkgBinaryCache | Out-Null
    }
    $env:VCPKG_DEFAULT_BINARY_CACHE = $script:VcpkgBinaryCache
}

function Get-OpenLocoExecutable {
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet('Release', 'Debug')]
        [string]$Configuration
    )
    return Join-Path $script:BuildDir (Join-Path $Configuration 'OpenLoco.exe')
}

function Test-LocomotionInstall {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )
    $g1 = Join-Path $Path 'Data\g1.DAT'
    return (Test-Path -LiteralPath $g1 -PathType Leaf)
}
