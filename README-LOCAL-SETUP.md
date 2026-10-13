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

## Isolated game verification

`--user_path` overrides the complete user root before logging starts. Ordinary launches retain the existing user directory. For disposable Rules checks use `build/block-rules-qa`, with a copied SV5 and its companions; never use original player saves as the test target.

```powershell
build/windows/Debug/OpenLoco.exe "D:\Users\GitHub\OpenLoco-Modded\build\block-rules-qa\save\Rules-test.SV5" --user_path "D:\Users\GitHub\OpenLoco-Modded\build\block-rules-qa" --locomotion_path "D:\SteamLibrary\steamapps\common\Locomotion"
```

Current Rules UI development is on `codex/native-inline-rules`, from tested D56 status checkpoint `4e1855d9`. `v26.09-modded.1` remains the checkpoint; master is unchanged. Assets and disposable verification saves stay outside commits.

The older D29-D56 notes below are historical; D57 at the end supersedes all instructions to open a separate Rules window, use its tab, or drop on permanent `+ drop inside` rows.

For manual block-rule checks, restart with the newly built executable. Existing running instances keep their old code. Rules sections resize using First row/Last row or the end grip. Rules and the normal Route/Orders tabs show actual nested blocks, current program and next stop/waypoint together, and support stop/waypoint dragging. The Times selector offers Forever or 1–20; Break's Loop selector chooses an enclosing Repeat. Else if checks the next condition and Else is the fallback. Keep white order circles visible while dragging to verify the updated numbering. User-run verification keeps this feature in TESTING until recorded.

Only Wait blocks have a **When** field for Before travelling or On arrival; ordinary branches, actions and loops choose their timing automatically and show no arrival/before label. Group keeps Name. Automatic Wait holds at its section's last stop/waypoint, using normal braking before its timeout starts. Automatic If and Repeat run before travel; actions follow their parent branch. For a short manual recheck, use a waypoint Wait with a false year condition and watch the train approach and brake, then use a cargo If to decide whether to visit a stop. Existing saved explicit timing still runs as saved. Station cargo handling should continue during a wait; next-trip vehicle-full checks run after loading. Save a disposable copy during approach or waiting and reload it with the matching companions. The v1 PHASES extension is readable by this branch; older builds reject it. Automated game launch and mouse control remain disabled at the user's request.

For the container fix, drag a stop/waypoint by `=` into `+ drop inside`; a selected run can use the same slot. Drag palette blocks onto orders inside a section to nest them. Restart the updated executable to get the new MakeCode-style shapes and matching nested drag handles.

For the direct-editing update, click a block's check/value socket rather than hunting for its bottom control. A click from Vehicle Route or shared-line Orders opens that exact field in Rules. Numeric/name fields open a native dialog; Repeat accepts `Forever` or `0` as well as 1–20. Both And/Or operands are clickable. Click a palette item to add it to the selected consecutive rows or focused section; **Actions** adds Else if/Else or removes the focused block. **Details** restores First row/Last row and the advanced buttons. The default 560x320 window leaves Details closed for more route space; 360x240 remains supported. Restart the rebuilt executable and use disposable saves for your manual check. No additional automated game interaction is scheduled.

For the manual Skip/live-path update, restart the rebuilt executable, keep Vehicle Route and Rules open and click Skip; both white arrows should immediately point at the new routable row, including across cargo instructions. In the shared-line window enable **Path** (or **All**) to see current rule-selected legs. The line's progress vehicle supplies current condition facts only. Path/All starts at the first row with fresh control state and omits inactive sections completely; the actual vehicle and progress arrows keep their existing latched execution state. Advancing orders or moving does not rebuild unchanged diagram geometry (D46). Paths build one missing leg per UI update and swap as complete snapshots; Updating means the last completed route stays visible while its replacement builds. The footer also explains wait boundaries, missing data or preview limits. False Waits stop the preview at their hold point; the preview does not forecast loading/occupancy or force physical movement/platform choice. Keep disposable matching save companions for manual checks. No automated game launch or mouse control is performed.

For the intermittent Path/All disappearance fix (D47), restart the rebuilt executable. Completed routes should stay visible during construction refreshes, including AI activity, until one complete replacement is ready. Line recolouring and hidden-line edits should not erase the route. A genuine removed track will disappear from the completed replacement; changing game/save clears old overlays immediately. This is a user manual visual check; no automated game interaction is scheduled.

Directional-waypoint signal fix (D48) is on `codex/directional-waypoint-signals`, based on the block-logic branch; master and `v26.09-modded.1` remain unchanged. Restart the rebuilt Debug/Release executable for your manual check using an isolated `--user_path` and a disposable save. Block the required waypoint route while leaving an unrelated exit free: Path, Chain and combined posts should display red and retain the order, then continue along that required route when clear. A genuine alternate route to the same waypoint remains allowed. Full suites passed 377/377 each; no automated game interaction was performed.

For the D49 circular-waypoint follow-up, restart the rebuilt executable on `codex/directional-waypoint-signals`. Recheck the screenshot layout: Else selects outer waypoint 3, another train blocks it, and inner waypoint 2's circle is free. The approaching train should wait red for the required route and resume directly after the blocker clears. Try a special post on a junction exit before path mode too. Genuine non-circular alternatives remain supported. Full Debug/Release suites passed 385/385 each; no automated game interaction was performed.

D50 (`4f5faaae`) also keeps the governing approach sprite red when the planner waits at an unsignalled decision beyond that post. Restart the rebuilt Debug/Release executable when convenient and manually check red while the required route is blocked, then clear-and-resume. Both builds and full suites passed (389/389 each; 49 focused signal checks). No automated game interaction was performed; the original-asset visual result remains unrecorded.

D51 (`3027c2d8`) on `codex/train-placement-crash` fixes an asymmetric-curve placement/reversal bounds defect capable of the reported Debug span assertion. Debug and Release builds and full suites passed (393/393 each; 53 focused placement/signal checks). Restart the rebuilt executable when convenient and retry the same curve endpoint with both pickup directions, ghost preview and train reversal. The user's log lacks a crash stack, so exact attribution remains unconfirmed until manual recheck. No automated game control was performed; master and `v26.09-modded.1` are preserved.

D52 (`049ef968`) on `codex/waypoint-signal-aspects` adds red during an incoming train's blocked-waypoint approach and retains it through the actual wait. The branch includes the prior placement fix. Debug/Release were reconfigured and built; both full suites passed 398/398 (58 focused signal/placement checks). The next startup log should identify `v26.09-modded.1-32`, branch `codex/waypoint-signal-aspects`, commit `049ef968`. Restart when convenient and manually recheck the blocked outer waypoint 3 versus free inner-circle case: red approaching and stopped, then required-route resumption when the blocker clears. No automated game control; master and `v26.09-modded.1` remain preserved.

D53 fixes the actual sprite-number mistake in the previous red override: SIGC3 frame 3 is yellow; native danger is frame 0. It also handles unplanned signalled junction arms with a free wrong exit. Both full suites passed 401/401; 61 focused signal/placement checks passed. The rebuilt startup stamp is `v26.09-modded.1-34`, branch `codex/waypoint-signal-aspects`, code `d4ed06d1`. Restart when convenient and recheck the actual red lamp on Path/Chain/combined while the required waypoint is blocked, then required-route resumption. Original files were inspected read-only, temporary art extractions removed and no game control performed. Master and the rollback checkpoint remain preserved.


D54 (`1a541252`, physical-entry follow-up `ab57d310` and placement safeguard `023ec618`) corrects unsafe passage after the user reported entering occupied track on D53. Pending reservations recheck physical occupation before planning admission, pass requests cannot bypass Path/Chain/combined reservations, and the physical front checks protected committed pieces before entering them. The latter also applies after the planner leaves path mode. Twelve new tests include seven failing native reproducers and ordinary-movement protection; 73 focused signal/placement checks passed. Reconfigured Debug/Release game builds and full serial suites passed 413/413 each. Configuration discovery checked all five executable paths and 69 signal tests per configuration; logs are `build/windows/signal-admission-{debug,release}-{build,tests}.log`. Temporary placement remains on its existing validated path. The final configure stamp is `v26.09-modded.1-38`, `codex/waypoint-signal-aspects`, `023ec618`. Restart when convenient for the user's manual stopping/red/resumption check, including a moved/placed wagon and a pass request. No automated game control; master and rollback checkpoint remain preserved.


D55 (`99ee01af`) follows the failed manual movement recheck on 023ec618. Physical checks now cover complete locally conflicting curves and movement endpoint overhangs, and survive ordinary-signal handoffs while rail logic is active. Five new tests bring focused coverage to 78/78; reconfigured Debug/Release game builds and full serial suites each passed 418/418. Startup stamp is `v26.09-modded.1-40`, branch `codex/waypoint-signal-aspects`, source `99ee01af`. Logs: `build/windows/signal-occupation-{debug,release}-{build,tests}.log`. Restart the final rebuilt executable for the user's long-curve/blocked-waypoint red-stop/resumption check. Master and the backup remain unchanged; no game launch or mouse control.

D56 (`cf47b6db`) fixes a reproduced unsafe waypoint boundary after the user confirmed D55 still ignored stopped wagons/reservations. A free waypoint before an occupied/reserved continuation no longer ends signal protection; Path/Chain/combined searches check to the safe waiting boundary. Advancing the waypoint replans unused continuation claims while retaining occupied holds. Focused checks passed 87/87; reconfigured Debug/Release game builds and full suites each passed 427/427. Startup stamp: `v26.09-modded.1-42`, `codex/waypoint-signal-aspects`, `cf47b6db`. Logs: `build/windows/waypoint-boundary-{debug,release}-{build,tests}.log`. Restart the rebuilt executable when convenient for the user's short blocked-waypoint/red-stop/resumption check, including stopped wagons just after the waypoint and reserved track. Signals remain TESTING; no automated game control. Master and rollback checkpoint are unchanged.

## Current inline Rules check (D57)

Restart the rebuilt executable when convenient; this task never launches or controls the game. In Vehicle Route and shared-line Orders, click the normal **Rules** button to expand the same window. The line list remains visible. Click a condition socket even with Rules off; dropdowns and number/name dialogs edit in that host. Rules reveals the independently scrolling 88-pixel palette, Actions, Details and bin. Drag an order by `=` onto the thin highlighted inside strip to grow a container, and use the section's right-hand closing grip to resize it. Consecutive row selections and whole section headers also drag; the pointer preview identifies the item. Escape/outside drops cancel. World number circles refresh after a valid order edit.

Only a short user visual check is requested: compact native appearance and readable fields, direct editing and dragging in both window types, and Rules-off/tab-change restoration without overlapping controls. Preserve disposable saves and matching companions if you choose an isolated profile. Phase 4 stays TESTING until you report visual acceptance; earlier outstanding gameplay/signal acceptance remains recorded separately.

The rebuilt startup stamp is `v26.09-modded.1-46`, branch `codex/native-inline-rules`, source `91037492`. Debug/Release complete suites each passed 447/447; full details and remaining acceptance are in `docs/DEVELOPMENT_STATUS.md`.
