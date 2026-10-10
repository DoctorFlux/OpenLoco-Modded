# Local Windows setup

Branch `dev/local-baseline` tracks OpenLoco `e71abfb3` plus the local framework plan. Project rules are in `AGENTS.md` and `docs/MASTER_PLAN.md`. These notes are for this machine. They do not change the original Locomotion installation.

## Machine

- Windows 11 Home 64-bit, build 26200
- Git 2.47.0.windows.2 at `C:\Program Files\Git\cmd\git.exe`
- CMake 3.30.5 at `C:\Program Files\CMake\bin\cmake.exe`
- Visual Studio Community 2022 17.14.3 at `C:\Program Files\Microsoft Visual Studio\2022\Community`
- MSVC 14.44.35207 (`cl` 19.44.35208.0), toolset `v143`
- Windows SDK 10.0.26100.0, selected by CMake for this OS
- Visual Studio 2019 is also installed. The `windows` preset pins `x64-windows-static-v143`, and this configure used the 2022 compiler
- The MFC component is not installed. This build did not need it. vcpkg's `atl` port is an empty package and completed successfully
- Ninja and GitHub CLI are not installed. The Windows preset uses the Visual Studio generator, so neither is required
- PowerShell 5.1 is the shell used by `scripts/windows`

Git and CMake were already new enough (CMake 3.22 or newer) and were not reinstalled.

## Repository

| Remote | URL |
| --- | --- |
| `origin` | `https://github.com/DoctorFlux/OpenLoco-Modded.git` |
| `upstream` | `https://github.com/OpenLoco/OpenLoco.git` |

`master`, `origin/master`, and `upstream/master` were all `e71abfb3` when `dev/local-baseline` was created. Nothing was force-pushed, and these local files are not committed.

## Original game data

The Steam install is `D:\SteamLibrary\steamapps\common\Locomotion`. `Data\g1.DAT` is there. OpenLoco's built-in search only looks on `C:`, so the `D:` library is found through the existing `loco_install_path` in `%APPDATA%\OpenLoco\openloco.yml`, or through `--locomotion_path`.

Assets were not copied into the repository. The Steam folder, including its existing `OpenLoco.exe`, `Loco.exe`, and saved games, was not modified.

Saves for this build go to `%APPDATA%\OpenLoco\save\`, not into the Steam folder.

## Build

vcpkg is not installed globally. The root `CMakeLists.txt` clones tag `d015e31e90838a4c9dfa3eed45979bc70d9357fc` into `build/windows/_deps` on the first configure. Binary archives are stored in `build/vcpkg-cache` via `VCPKG_DEFAULT_BINARY_CACHE`, which stays on `D:` and is covered by the existing `build/` gitignore rule.

From the repository root:

```powershell
$env:VCPKG_DEFAULT_BINARY_CACHE = "D:\Users\GitHub\OpenLoco-Modded\build\vcpkg-cache"
cmake --preset windows
cmake --build --preset windows-release
```

The same steps are wrapped by the scripts below.

| Result | Location |
| --- | --- |
| Release executable | `D:\Users\GitHub\OpenLoco-Modded\build\windows\Release\OpenLoco.exe` |
| Debug executable | `D:\Users\GitHub\OpenLoco-Modded\build\windows\Debug\OpenLoco.exe` |
| Solution | `D:\Users\GitHub\OpenLoco-Modded\build\windows\openloco.sln` |

## Scripts

All of these are safe to run again. They do not write into the Steam folder.

| Script | What it does |
| --- | --- |
| `scripts/windows/setup-env.ps1` | Checks Git, CMake, Visual Studio 2022, MSVC, the Windows SDK, and `Data\g1.DAT`. Writes gitignored Cursor settings. |
| `scripts/windows/build.ps1` | Configures `windows`, then builds Release and Debug. `-Configuration Release` or `Debug` builds one. `-SkipConfigure` skips generation. |
| `scripts/windows/test.ps1` | Runs `ctest -C Release` in `build/windows`. |
| `scripts/windows/launch.ps1` | Starts the Release executable with `--locomotion_path`. `-Configuration Debug` starts the debug executable instead. |

Cursor settings live in `.vscode/` and are already gitignored:

- `settings.json` uses the `windows` configure preset and the `windows-debug` build preset
- `tasks.json` runs the build, test, and launch scripts
- `launch.json` debugs `build/windows/Debug/OpenLoco.exe` with `cppvsdbg` and passes the Steam path

`.gitignore` already ignores `build/`, `.vscode/`, and `vcpkg/`. No extra ignore rule was required.

## What was verified

- `cmake --preset windows` finished with exit code 0. vcpkg installed its 13 packages in 6.7 minutes. CMake selected MSVC 19.44.35208.0 and Windows SDK 10.0.26100.0.
- `cmake --build --preset windows-release` finished with exit code 0. `App.vcxproj` produced `build\windows\Release\OpenLoco.exe` (7,672,832 bytes).
- `ctest --test-dir build/windows -C Release --output-on-failure --parallel` reported `100% tests passed, 0 tests failed out of 136`.
- `cmake --build --preset windows-debug` finished with exit code 0 and produced `build\windows\Debug\OpenLoco.exe` (27,667,456 bytes).
- A second `scripts/windows/build.ps1 -SkipConfigure -Configuration Release` completed without rebuilding sources.
- The Release executable was started with `--locomotion_path D:\SteamLibrary\steamapps\common\Locomotion`. The log `%APPDATA%\OpenLoco\logs\openloco_2026-10-10_10_44_15.log` recorded:

```
Using Locomotion install path: D:\SteamLibrary\steamapps\common\Locomotion
Using save path: C:\Users\mario\AppData\Roaming\OpenLoco\save\
OpenAL 1.1 ALSOFT , Vendor: OpenAL Community, Renderer: OpenAL Soft, initialized.
```

The process was still running after audio initialisation and `OpenLoco::initialise()`, which opens `Data\g1.DAT`. It was then stopped. No scenario was loaded.

Startup applies the saved borderless display mode and rewrites `openloco.yml`. That file was restored from the copy taken before launch, and it again matches that copy, including `loco_install_path`. `game.cfg`, `scores.dat`, and `plugin.dat` were unchanged. The AppData save folder still has the same one file. The game created an empty `%APPDATA%\OpenLoco\objects` directory because that folder did not exist yet.

The private upstream simulate/compare job was not run. It needs the private `OpenLoco/LocomotionAssets` and `OpenLoco/TestData` repositories.

## Still worth doing by hand

Open the Release executable and load a scenario, to confirm the window draws the original graphics. From the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\windows\launch.ps1
```

In Cursor, the **OpenLoco Debug** launch configuration debugs the Debug build against the same Steam folder.
