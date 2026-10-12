# Testing plan

GoogleTest is the runner. `ctest --test-dir build/windows -C Debug` or `-C Release` executes it. New libraries register tests through `loco_add_library`.

The private upstream simulate/compare job needs repositories this fork does not have. Do not treat it as a local requirement.

Tests must not open the Steam Locomotion directory or `%APPDATA%\OpenLoco`.

## Existing coverage

136 tests passed in Release before the modding library existed. They cover core utilities, math, diagnostics, and `TileManagerTests`. They do not cover orders, signals, vehicles, saves, or mods.

## Required as each milestone lands

| Area | Tests |
| --- | --- |
| Manifest | Valid file, missing field, bad version, duplicate key, self-dependency, unicode escape, API rule |
| Dependencies | Order, missing dep, cycle, conflict, optional present, optional absent |
| Discovery | Directory of mods, single root, bad JSON, incompatible API, oversized file, symlink skipped |
| Packages | Install, upgrade, uninstall, corrupt zip, path escape rejected |
| Saves | SV5 import, new file written elsewhere, checksum, missing mod, migration |
| Lines | Two vehicles share one edit; a vehicle with no line keeps vanilla orders |
| Timetables | Early vehicle waits; late vehicle does not freeze the game |
| Logic | No blocks matches current order advance. A group of two stops and one waypoint is skipped or visited from a cargo check. Go to stays on the same list. Repeat is bounded. Two vehicles on one line share blocks and keep separate counts. Step budget, unavailable check, deterministic next order |
| Consists | Uncouple, couple either end, parked wagons remain, reverse |
| Reservations | Crossing paths conflict; parallel paths do not; release on cancel |
| Shunting | Scenarios below |
| Cargo and passengers | Classic mode unchanged; destination mode covered by its own tests |
| Limits | `TileManagerTests` updated only when map constants change, in the same change |

## In-game scenarios before a feature is complete

- One shunter sorts mixed wagons into two sidings.
- Two shunters work one yard without taking the same wagon.
- A locomotive runs around its train and the service id stays.
- A train pushes wagons into a siding and stops before an occupied piece.
- Two trains cross a junction on non-overlapping paths.
- A save is loaded while a shunt job is mid-move, after the save container exists.
- A freight train requests wagons and a shunter assembles them.
- Passengers transfer between two lines.

## Determinism

For logic, reservations, and jobs, run the same command list twice and compare the resulting state. Do not compare frame times.

## Status

On 2026-10-10, `ctest --test-dir build/windows -C Debug -R "ActivationTests|ManifestTests|DependenciesTests|DiscoveryTests"` passed 22 tests and failed 0. `OpenLoco.exe mods --mods_path` then logged `Hello World from openloco.sample.hello` and exited 0. Symlink skipping is implemented and not covered by an automated test. Zip loading is not implemented.

## Block-rules implementation (2026-10-12, TESTING)

The native Rules subsystem is implemented on `codex/block-logic-programming`. See `mods/PROGRAMMABLE_LOGIC.md` for current controls, limits, runtime and companion behavior; see `DEVELOPMENT_STATUS.md` for actual verification and remaining manual checks. Geometry supports default 560x320 and compact 360x240, with a pinned palette, scrolling list, section grips, bracket dragging, deletion bin, and mouse-following dragged-item label. Commands validate before simulation mutation. No map tool is claimed by Rules dragging.

Run reconfigured Debug and Release builds and serial full CTest suites. Rules tests cover validation, ownership, section edits, cargo thresholds, compartment fullness, If/Else if/Else, nested repeats, budget pauses, timeouts, shared counters, platform geometry/wagon occupation/self exclusion, companion corruption/mismatch/reused IDs, cloning and detachment. Native mouse interaction and physical route/cargo behavior still require the isolated in-game matrix; unit tests alone do not complete that matrix.

Current results: Debug and Release each passed **329/329**, including 126 rules tests, with configuration-specific discovery (2026-10-12). Use serial CTest; parallel runs can trip the existing timer timing assertion. Native GUI checks use the isolated profile documented in `README-LOCAL-SETUP.md`. Do not operate its mouse while the user is playing. Record each manual acceptance row from `DEVELOPMENT_STATUS.md`, including pending physical skip/diversion, waypoint waits, continued station cargo handling, complete section drag/cancellation/bin, save/reload and the multi-train Schemes/Own check. Passing unit tests does not clear those rows.

Additional regression coverage checks growing a branch with actions and paired Else, atomic crossing rejection, latched green/red feedback, wait feedback and valid saved state, stale/foreign/outside/Escape drag cancellation, and consistent nested block-layout hit testing and scrolling. Latest native controls are tested manually by the user. Do not launch or operate the game automatically for these checks; provide the built executables and record feedback without claiming those manual checks have passed.

Latest regression coverage includes Else-if short circuiting and invalid chain rejection, infinite repeats with conditional Break and fresh entry, budget-paused infinite decisions, invalid/removed loop targets, saved zero counters, simultaneous program/next-listed feedback, maximum-size wrapped layouts, and private/shared stop plus waypoint drags regenerating world-number frames. The loader originally rejected saved zero counters; this regression is fixed and covered.

User manual checks for this extension: inspect proper nested blocks and green/red progress in all three windows; confirm next stop/waypoint stays visible while waiting; try Else if then Else and Forever with a conditional Break; drag private/shared stops and waypoints while world circles are visible. Repeat at 360x240 and ordinary vehicle/line sizes. Record actual results in development status. No automated game launch or mouse control is scheduled.

Container regression coverage adds explicit edge insertion, adjacent membership changes, all container kinds, nested ancestor/action/Else-chain propagation, selected-section insertion, ownership-preserving private/shared commands, round-trip/reached-stop preservation, exact grip/drop-slot coordinates and native software-raster checks for rounded notches and C mouths. The raster checks use no original assets and do not count as in-game acceptance. Earlier focused result: 75/75 passed. Final configuration-specific full Debug and Release suites each passed 279/279, including all 76 rules tests; details are in development status. User manual recheck: drag multiple stops/waypoints into closing slots, add palette blocks on interior rows, then inspect MakeCode-style shapes and joined Else arms in all three views. No automated game mouse input is scheduled.

GoogleTest discovery uses PRE_TEST so multi-configuration builds generate separate test lists. Verify `OpenLocoTests[1]_tests-Debug.cmake` points to the Debug executable and its Release counterpart points to Release; the same applies to Core/Math/Diagnostics/Utility tests. The prior shared POST_BUILD list could make configuration labels misleading. Keep suites serial for the shared temporary companion names, and finish link/rebuild operations before running tests.

Arrival-timing regression coverage adds 26 tests: no condition evaluation at planned arrival; physical speed/routing/subposition gates; distinct before-travel and arrival behavior; inherited actions/Else chains/fresh Repeat visits/Break; clocks starting at reached boundaries; cargo processing while waiting and future-trip checks deferred until loading; independent shared waits; saved approaches, waits and arrival budget pauses; legacy premature waits and malformed phase extensions; and edits removing stale approach state. Final Debug/Release full suites each passed 305/305. Logs are `build/windows/rule-arrival-{debug,release}-{build,tests}.log`. Unit boundary tests do not verify the full rendered/native braking curve.

Keep manual rechecks short and user-run: watch a waypoint Wait brake naturally, compare **When > Before travelling** against **On arrival** on a cargo condition, then save/reload a disposable copy during approach or waiting. Confirm station cargo continues and departure fullness sees the loaded vehicle. Record real outcomes in development status alongside existing acceptance rows. No automated game launch or input is scheduled.

White-arrow regression coverage adds ten tests for real rule-selected destinations, Repeat returns, cargo/program cursor divergence at reached holds, approach and budget state, independent shared vehicles, edit/deletion/vanilla fallback, saved waits, nested/wrapped coordinates and selected/scrolled software raster visibility. Focused Debug suite passed 10/10; final full Debug and Release suites each passed 315/315. The Release test PDB was rebuilt before verification. Logs are `build/windows/rule-arrow-{debug,release}-{build,tests}.log`, `rule-arrow-focused-tests.log` and `rule-arrow-release-test-rebuild.log`. User manual recheck: the outlined white arrow should track the selected destination and retain the reached station/waypoint during holds in Vehicle Route, shared-line Orders and Rules, independently of the active program stripe and next-listed footer.

Direct-field editing adds fourteen regressions: shared field hit/render bounds, separate primary/secondary values, atomic invalid text rejection, Repeat/Wait/group-name ranges and full dialog initial values, stale owner/key/revision guards, every item in paged selectors, compact canvas/shape bounds and maximum block layouts, private/shared command integration and companion round-trip, palette/Else-chain actions, and unchanged values preserving live wait expiry. Final serial Debug and Release suites each passed **329/329**, including **126 rules tests**. Logs: `build/windows/rule-inline-{debug,release}-{build,tests}.log`. Tests cover geometry and command helpers; they do not drive native GUI events.

Short user manual check: click a check/value/And-Or field in Rules and in each normal order tab; edit both operands, Repeat to Forever and a group name; click a palette item, then Actions to add Else if/Else; toggle Details at 560x320 and 360x240 and scroll nested sections. Long stop/group menus should reach their final page. Confirm dialogs edit the intended shared/private owner, fields remain readable and controls avoid overlap. Recheck drag/inside-slot cancellation if affected. Keep earlier braking, white-arrow, save/reload and Schemes acceptance rows pending until the user records them. No automated game launch/mouse control is scheduled.
