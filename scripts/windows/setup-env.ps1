# Detect the local Windows toolchain and write gitignored Cursor C++ settings.
# Safe to run repeatedly. Does not install software and does not modify the
# original Locomotion installation or %APPDATA%\OpenLoco saves.
[CmdletBinding()]
param(
    [string]$LocomotionPath = 'D:\SteamLibrary\steamapps\common\Locomotion'
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'OpenLoco.Common.ps1')

function Test-CommandOnPath {
    param([Parameter(Mandatory = $true)][string]$Name)
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }
    return $null
}

$missing = New-Object System.Collections.Generic.List[string]

$git = Test-CommandOnPath 'git'
$cmake = Test-CommandOnPath 'cmake'
if (-not $git) { $missing.Add('git') }
if (-not $cmake) { $missing.Add('cmake') }

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = $null
$msvc = $null
if (Test-Path -LiteralPath $vswhere) {
    $vsPath = & $vswhere -version '[17.0,18.0)' -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($vsPath) {
        $msvc = Get-ChildItem -LiteralPath (Join-Path $vsPath 'VC\Tools\MSVC') -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending |
            Select-Object -First 1
    }
}
if (-not $vsPath) { $missing.Add('Visual Studio 2022 with the MSVC x64 toolset') }
if (-not $msvc) { $missing.Add('MSVC toolset under Visual Studio 2022') }

$sdkRoot = 'C:\Program Files (x86)\Windows Kits\10\Include'
$sdk = $null
if (Test-Path -LiteralPath $sdkRoot) {
    $sdk = Get-ChildItem -LiteralPath $sdkRoot -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending |
        Select-Object -First 1
}
if (-not $sdk) { $missing.Add('Windows 10/11 SDK') }

$g1Ok = Test-LocomotionInstall -Path $LocomotionPath
if (-not $g1Ok) {
    $missing.Add("Locomotion Data\g1.DAT under $LocomotionPath")
}

Set-OpenLocoBuildEnvironment

$vscode = Join-Path $script:RepoRoot '.vscode'
if (-not (Test-Path -LiteralPath $vscode)) {
    New-Item -ItemType Directory -Force -Path $vscode | Out-Null
}

$utf8 = New-Object System.Text.UTF8Encoding $false

$settings = @'
{
    "cmake.configurePreset": "windows",
    "cmake.buildPreset": "windows-debug",
    "cmake.useCMakePresets": "always",
    "C_Cpp.default.configurationProvider": "ms-vscode.cmake-tools"
}
'@

$tasks = @'
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "OpenLoco: Configure and build",
            "type": "shell",
            "command": "powershell",
            "args": [
                "-NoProfile",
                "-ExecutionPolicy",
                "Bypass",
                "-File",
                "${workspaceFolder}/scripts/windows/build.ps1"
            ],
            "group": {
                "kind": "build",
                "isDefault": true
            },
            "problemMatcher": "$msCompile"
        },
        {
            "label": "OpenLoco: Test Release",
            "type": "shell",
            "command": "powershell",
            "args": [
                "-NoProfile",
                "-ExecutionPolicy",
                "Bypass",
                "-File",
                "${workspaceFolder}/scripts/windows/test.ps1"
            ],
            "group": {
                "kind": "test",
                "isDefault": true
            },
            "problemMatcher": []
        },
        {
            "label": "OpenLoco: Launch Release",
            "type": "shell",
            "command": "powershell",
            "args": [
                "-NoProfile",
                "-ExecutionPolicy",
                "Bypass",
                "-File",
                "${workspaceFolder}/scripts/windows/launch.ps1"
            ],
            "problemMatcher": []
        }
    ]
}
'@

$jsonLocomotionPath = $LocomotionPath.Replace('\', '\\')
$launch = @"
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "OpenLoco Debug",
            "type": "cppvsdbg",
            "request": "launch",
            "program": "`${workspaceFolder}/build/windows/Debug/OpenLoco.exe",
            "args": [
                "--locomotion_path",
                "$jsonLocomotionPath"
            ],
            "cwd": "`${workspaceFolder}/build/windows/Debug",
            "stopAtEntry": false
        }
    ]
}
"@

function Write-Utf8File {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Content
    )
    $normalized = $Content.TrimEnd() + "`n"
    if (Test-Path -LiteralPath $Path) {
        $existing = [System.IO.File]::ReadAllText($Path)
        if ($existing -eq $normalized) {
            return
        }
    }
    [System.IO.File]::WriteAllText($Path, $normalized, $utf8)
}

Write-Utf8File -Path (Join-Path $vscode 'settings.json') -Content $settings
Write-Utf8File -Path (Join-Path $vscode 'tasks.json') -Content $tasks
Write-Utf8File -Path (Join-Path $vscode 'launch.json') -Content $launch

Write-Output "Repository: $script:RepoRoot"
Write-Output "Git: $(if ($git) { $git } else { 'MISSING' })"
Write-Output "CMake: $(if ($cmake) { $cmake } else { 'MISSING' })"
Write-Output "Visual Studio 2022: $(if ($vsPath) { $vsPath } else { 'MISSING' })"
Write-Output "MSVC: $(if ($msvc) { $msvc.FullName } else { 'MISSING' })"
Write-Output "Windows SDK: $(if ($sdk) { $sdk.Name } else { 'MISSING' })"
Write-Output "Locomotion g1.DAT: $(if ($g1Ok) { 'found (read-only check)' } else { 'MISSING' })"
Write-Output "vcpkg binary cache: $script:VcpkgBinaryCache"
Write-Output "Cursor settings: $vscode (gitignored)"

if ($missing.Count -gt 0) {
    Write-Error ("Missing required pieces: " + ($missing -join '; '))
}
