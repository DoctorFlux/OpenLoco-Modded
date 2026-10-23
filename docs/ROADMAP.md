# Roadmap

D71 automated verification: source `04b8a699` builds both games; full Debug/Release suites each pass **583/583** (199 Rules, 113 CargoDist), zero failures. Follow-up remains **TESTING** for the user's short caption-drop/Jump-if check in both native order windows. Work stays directly on master; the .3 release checkpoint and deferred phases are unchanged. Actual times, initial corrections, warnings and evidence are in development status.

D71 (2026-10-23): the user selects order-block dragging and OpenTTD-style conditional jumps as the next follow-up, directly on master rather than a new branch. Caption drags move orders into multi-row/nested private/shared containers; Shift-drag selects ranges. Jump if and native vehicle comparisons extend the same Rules interpreter/palette, previews and CargoDist connectivity. Verification/results and the short user-only acceptance gate are in development status. This does not resume deferred save-container, consist or shunting phases.

D69 release update (2026-10-15): **26.09-modded.3 is the first consolidated modded edition (edition 1)**, including accepted CargoDist on master plus shared lines, timetables, path/chain signals and the first visual-logic delivery. It supersedes modded.1/modded.2; the older GitHub releases and local/origin tags were removed after the new package and downloaded assets were verified. Commit history remains; D70 (2026-10-21) subsequently removes the fully merged CargoDist branch locally and from origin. Release notes: [26.09-modded.3](releases/v26.09-modded.3.md). Fresh tagged Debug/Release suites each pass 566/566; package, downloaded-asset and retirement evidence is recorded in development status. This supersedes earlier current-release/checkpoint-preservation statements; their historical tests remain valid. Save-container work, consists and shunting remain deferred.

CargoDist delivery is **COMPLETED (user accepted D68, 2026-10-14)**, including the implemented Phase 9/10 portions. The complete CargoDist branch and acceptance documentation were merged normally into master through `eea93c8d`; both release tags and the feature branch are retained. Source `2d3bb93c` already passes reconfigured game builds and 566/566 tests in each configuration. Merged source/data/build inputs match that tested tree; the recorded test evidence applies. No new release, warehouse type, order or save format is added. Next concrete task is the user's next requested nondeferred feature on a fresh branch from master. Consists/shunting/save-container work remains deferred. Later TESTING/manual gates in historical checkpoint records are superseded for CargoDist by D68.

Each milestone is independently buildable. Do not land a whole phase as one change. Status values are defined in `DEVELOPMENT_STATUS.md`.

## Phase 0 — Baseline

Done for commit `e71abfb3`.

- Local Windows build, tests, and launch notes: `README-LOCAL-SETUP.md`.
- Vanilla source map: `DEVELOPMENT-ROADMAP.md`.
- This documentation set.

Maintenance follow-up D51 (`3027c2d8`) addresses the user's placement-time span assertion on `codex/train-placement-crash`, based on the tested signal branch. Rail placement, ghost preview and reversal share a checked reverse movement-position conversion for curves with unequal direction lengths. Full Debug/Release suites each passed 393/393; 53 focused placement/signal checks passed. The exact reported stack is unavailable. Next task is the user's short curve-end placement/preview/reversal recheck. Native acceptance remains user-run; this does not start a later gameplay milestone.

## Phase 1 — Mod loader

Removed. Shared lines, timetables, and path and chain signals are part of the game. There is no manifest loader, Mods window, or Hello World sample.

## Phase 3 — Shared lines and timetables

Implemented for the line record, assignment, vanilla order types, save companion, line manager window, per-stop departure and platform data, and timetable decisions. Movement of a vehicle on a line advances through that line and reverses at the end of a point-to-point line. Platform choices are stored; they are not yet forced onto the track pathfinder.

Specs: `docs/mods/SHARED_LINES.md`, `docs/mods/TIMETABLES.md`.

## Phase 4 — Block rules

Status: **COMPLETED (user accepted 2026-10-14, D59)**. The complete logic branch is integrated into master by merge `28e64e62`. Original Phase 4 checkpoint is on `codex/block-logic-programming`; the current inline UI follow-up is on `codex/native-inline-rules`. Model, runtime, native editor and v1 persistence are implemented. Delivery acceptance is recorded; earlier individual gameplay checks remain documented separately. Next review Phase 5 dependencies on a fresh feature branch. The normal order tabs now show actual nested block headers/brackets, current program plus next destination and conditional colours; blocks resize across multiple rows, and both normal lists accept validated order dragging. Thin insertion strips grow containers and nested grips follow their visible position; D57 supersedes the standalone workflow with compact native C blocks and medium-font sockets. Release `v26.09-modded.1` is the checkpoint before this phase. Spec: `docs/mods/PROGRAMMABLE_LOGIC.md`.

The phase is done when a vehicle and a shared line can both run the blocks, including a group of stops and waypoints, from the native inline editor. A runner without a complete player editor, or editing only one of the two lists, is not this phase.

Arrival timing is implemented: only Wait exposes When > Before travelling/On arrival; ordinary new blocks use automatic timing, and existing saved choices remain honored; waits finish native physical braking at their stop/waypoint before arming, arrival checks latch before cargo transfers, and next-trip checks run after loading. Current future-stop facts can drive advance skips/diversions. The optional v1 PHASES extension resumes approaches and arrival decisions. Next concrete task remains user manual acceptance, now including braking/arrival timing and save/reload during approach; do not proceed to Phase 5 yet.

Historical D38 route-arrow work used a shared read-only committed travel/hold key in all three views, with a visible outlined white marker on the nested order row (D38). Recheck it during Skip/Go to, Repeat, waits and cargo handling in the user's next manual run; the program marker and provisional next-destination footer serve their separate purposes.

Historical D39-D41 editing included clickable fields in all three block views, native value dialogs/dropdowns, paged long selectors, palette clicks and Else/Else-if/removal Actions. Rules defaults to 560x320 with optional Details and the same 360x240 minimum (D39–D41). Final full Debug and Release suites passed 329/329 each. Next concrete task is the user's short direct-editing/compact-layout check followed by the outstanding manual acceptance rows; Phase 4 remains TESTING.

Manual Skip now retains the arrow immediately in Vehicle/Rules (D43). Shared-line Path/All now projects current rule choices without changing simulation, with periodic selected-vehicle fact refresh, one missing leg search per UI update and a bounded geometry cache (D44). The latest full Debug/Release suites each passed 349/349. Next concrete task adds user manual arrow/live-map responsiveness checks to the remaining Phase 4 acceptance rows.

Path/All condition display now swaps complete per-line snapshots, stabilizes lanes and bounded All admission, and includes an already-selected loading departure (D45). Actual simulation is unchanged. Final Debug/Release suites passed 357/357 each. Next concrete task is the user's short visual condition-change recheck; Phase 4 remains TESTING.

Phase 4 follow-up D46 separates whole-line Path/All diagrams from copied vehicle execution progress. It evaluates the first row with fresh state, omits inactive If/Else stops and waypoints entirely, and removes moving-origin legs. Current facts still refresh live through the bounded cache/publication path. Full Debug/Release suites each passed 360/360. Next concrete task: user recheck the stop 1/waypoint 2 versus Else waypoint 3 example with waypoint 4 after the branch, then finish remaining manual acceptance. Keep TESTING.

D47 follow-up prevents completed Path/All routes disappearing during construction/cache refreshes, separates metadata invalidation and isolates hidden-line changes. Save/game resets clear publications immediately. Implementation checkpoint `e42b1da8` is pushed; latest full Debug/Release suites each passed **365/365**, including **162 rules tests**. Model/commands, runtime, all three block views, persistence and the requested follow-up fixes are implemented. Next concrete task: user recheck the previously intermittent same-route disappearance and exclusive passenger If/Else route in the rebuilt executable, then complete the remaining [manual acceptance](DEVELOPMENT_STATUS.md#outstanding-user-acceptance). Keep Phase 4 TESTING; no new milestone or merge starts from this documentation update.

1. Block list, checks the game can already answer, If/Else if/Else, Skip, Go to, finite/Forever Repeat, Break, and the 256-step budget. Tests load the blocks from memory.
2. Run those blocks on a vehicle's own orders, and run the same blocks on a shared line. Each vehicle on a line keeps its own repeat count and wait.
3. Inline Rules editor (D57). A normal Rules button expands the vehicle Route/shared-line Orders host; compact native C blocks, readable condition fields and full drag/edit tools preserve existing controls. The line browser stays visible.
4. Save `<save>.SV5.orules` when the tests above pass. A missing file leaves the orders as they are. This does not wait for phase 5 and does not widen `S5::GameState`.
5. Passenger, freight-destination, and yard blocks stay out of the palette until those features exist. They extend this shared editor.

## Current priority - CargoDist (D62)

Before deferred Phases 5/6/8, the implemented TESTING milestone is the approved combined passenger/mail/freight CargoDist milestone: bounded demand/flow routing, native cargo lifecycle and transfer accounting, matching companion persistence, compact native Cargo summaries, Automatic/Classic preset and Rules checks. All cargo types default to automatic, including older saves. Implement on `codex/cargo-distribution`; starting full-suite baseline 447. No physical coupling, yard jobs or new save container. Spec: `mods/CARGO_DISTRIBUTION.md`.

## Phase 5 — Save container

1. Re-read upstream NSF notes.
2. Specify chunks and migrations in `SAVE_FORMAT.md` without changing `S5::GameState` until the reader is tested.
3. Import SV5 into the new container and write a new file.
4. Round-trip test against `GameSaveCompare` for the vanilla subset.
5. Store mod id, version, and state-schema for each active mod.

## Phase 6 — Consists

1. Stable consist id separate from the vehicle head index.
2. On-track couple and uncouple commands with failure codes.
3. Parked wagon groups remain entities.
4. Reverse and run-around use real movement.
5. Tests: split, couple from either end, save mid-operation once phase 5 can store it.

## Phase 7 — Reservations and signals

Implemented ahead of the save container, using the `.osignals` companion (D20). Spec: `docs/mods/PATH_SIGNALS.md`.

1. Reservation holds keyed by track piece and owner. Implemented.
2. Conflict test for junctions, crossings, and parallel tracks. Implemented with quarter-tile clearance. Pushed wagons wait for consists.
3. Path and chain flags on the normal signal, combined with the existing one-way and two-way buttons. Block signals stay the default. Implemented.
4. Deadlock detection that stops and reports instead of freezing. Not implemented; trains waiting for a path retry every 16 ticks.

Directional-waypoint correction (D48) is implemented at `a728d749` on `codex/directional-waypoint-signals`, based on the preserved block-logic branch. Required waypoint reachability prevents a blocked route falling back to an unrelated exit; the train retains its order and waits red. Proof/search and reservation length remain bounded. Full Debug/Release suites each passed 377/377; 37 focused signal checks passed. Next concrete task is the user's short blocked-route/clear-and-resume native check. Signals remain TESTING; no later milestone or merge starts here.

D49 screenshot follow-up is implemented at `b9f7eb33` on the same signal-fix branch: a free circle returning to the same directed junction cannot substitute for the required waypoint route, and facing special junction arms gate selection before path mode. Full suites passed 385/385 each; native screenshot-layout recheck is the next task. Phase 4 and signals remain TESTING.

D50 (`4f5faaae`) fixes yellow waiting sprites when the planner is beyond the approach post. Failed path decisions mark the nearest facing special post in the bounded routing ring, stop at block boundaries and clear on successful progression. Full Debug/Release suites passed 389/389 each; 49 focused signal checks passed. Next task is the user's short red-sprite/clear-and-resume check in the screenshot layout. Signals remain TESTING.

D52 (`049ef968`) on `codex/waypoint-signal-aspects` adds red during approach before a refused waypoint reservation, with independent approaching/waiting display owners and bounded 16-tick probes. This branch also includes the placement fix. Debug/Release full suites each passed 398/398; 58 focused signal/placement checks passed. Next task is the user's red-during-approach-and-wait screenshot-layout recheck, then required-route resumption after clearing the blocker. Signals remain TESTING.

D53 (`d4ed06d1`), same waypoint-aspect branch, corrects the previous override's actual sprite index: frame 3 was yellow on SIGC3; native danger is frame 0. It also probes unplanned signalled junction arms so a free wrong exit cannot clear a blocked required waypoint. Original art was inspected read-only, with no game control or bundled assets. Full Debug/Release suites each passed 401/401; 61 focused signal/placement checks passed. Next task is the user's actual-red-lamp check for all three kinds. Signals remain TESTING.

D54 (`023ec618`, following `1a541252` and `ab57d310`), same waypoint-aspect branch, follows the user's report of entry into occupied track on D53. Actual native regressions reproduced unsafe pending reuse and special-signal pass-counter bypasses. Remaining pending steps now recheck physical wagons/holds before admission; Path/Chain/combined passage always requires a safe reservation, including junctions. Failed checks preserve occupied holds/orders and wait red. The physical front separately rechecks protected committed pieces before entry, including after the planner leaves path mode. Focused checks passed 73/73; full Debug/Release suites each passed 413/413. Temporary placement motion remains outside the live entry guard. Next task is the user's manual occupied-route stopping/resumption check, including a moved/placed wagon and pass request. Signals remain TESTING; no milestone advancement or merge.

D55 (`99ee01af`) follows the user's failed D54 movement recheck. Complete local conflicting-piece occupation and native endpoint footprints replace the incomplete local-tile fallback inherited from master. Physical entry remains protected through ordinary-signal handoffs while rail logic is active. Focused checks passed 78/78, including private/shared Path/Chain/combined admission and every native movement sample; full Debug/Release suites each passed 418/418. Next task is the user's real long-curve/blocked-waypoint stopping and resumption check. Retain TESTING; no merge or milestone advancement.


D56 (`cf47b6db`), same branch, follows another failed D55 manual stopping check. A waypoint no longer ends signal protection before occupied/reserved track: bounded safety checks continue to the safe waiting boundary for all three kinds. Native waypoint advancement replans unused continuation claims without releasing occupied holds. Nine new checks cover stopped wagons, foreign reservations, shared orders/resumption, later orders, Chain exits, safe boundaries, bounds and station compatibility. Focused checks passed 87/87; both rebuilt full suites passed 427/427. The next task is the user's short waypoint-before-blocker/red-stop/resumption check; keep signals TESTING.

## Phase 8 — Shunting (DEFERRED)

1. Job records and state machine.
2. Single shunter sorts wagons.
3. Two shunters in one yard.
4. Run-around and push into a siding.
5. Yard rules and shunter rules using the same inline Rules editor.
6. Freight requests only if cargo demand is also loaded.

## Phase 9 — Cargo demand

The CargoDist portion is COMPLETED (user accepted D68): automatic asymmetric freight, compatible services, nonaccepting interchanges, destinations, linked earnings and persistence. Manual is selectable. Spec: `mods/CARGO_DISTRIBUTION.md`. Industry markets and production scheduling in `mods/CARGO_DEMAND.md` remain future extensions.

## Phase 10 — Passengers

The CargoDist portion is COMPLETED (user accepted D68): automatic symmetric passengers/mail, multiple station destinations, two-way multimodal feeders, aggregated packets, transfers, native views and persistence. Manual is selectable. Spec: `mods/CARGO_DISTRIBUTION.md`. New population/generation models in `mods/PASSENGER_DESTINATIONS.md` remain future extensions.

## Phase 11 — Objects and worlds

After NSF/NOF direction is rechecked: raise caps, variable map sizes, generation controls. Specs: `docs/mods/EXPANDED_OBJECT_LIMITS.md`, `docs/mods/LARGER_WORLDS.md`, `ENGINE_LIMITS.md`.

## Phase 12 — Release

Profile delivered features, build native release artifacts, write setup instructions, and keep original assets out of the artifacts. No loader or `.olmod` packaging is part of the current scope.

## Parallel work that is safe now

- Documentation and block-rule tests.
- Block-rule tests that do not touch `GameState`.
- Line and timetable data-model tests in memory.
- Upstream tracking when OpenLoco publishes NSF or NOF notes.

Not safe in parallel: editing `S5::GameState`, changing map constants, or hooking the loader into the title screen.

D57 inline UI follow-up is implemented through `91037492` (initial integration `bcec1e98`, selection/scroll lookup `5ca5ceaf`) on `codex/native-inline-rules`, based on D56 status checkpoint `4e1855d9`. It replaces the separate Rules workflow with per-window sessions, compact native block rendering and responsive scroll thumbs/offsets. Reconfigured Debug/Release game builds passed; complete serial suites each passed 447/447, with 182/182 focused Rules tests. Next task: the user's short appearance/field-edit/drag check in both hosts, then record acceptance. Keep Phase 4 TESTING and preserve master/checkpoint.

D58 native-order presentation follow-up is implemented at `be510fcb` on the same inline Rules branch. Stops/waypoints use the base-game captions, number circles and plain list background inside shaped logic. Reconfigured Debug/Release game builds passed; complete suites passed 447/447 each and focused Rules 182/182. Next task is the user visual recheck of order rows in both hosts. Phase 4 remains TESTING.

D59 supersedes the prior Phase 4 TESTING/merge gate: the user accepted the delivered step and explicitly requested master integration on 2026-10-14. Integrate the complete published logic branch without rewriting history and retain `v26.09-modded.1`. This merge does not implement Phase 5 or create a release.

D59 integration result: normal merge `28e64e62` and accepted feature head `1371ff4d` were pushed to origin. No conflicts or source/build-input changes from tested `be510fcb`; rollback preserved. Next task is Phase 5 dependency review on a fresh feature branch.

D60 prepares accepted Phase 4 for release `v26.09-modded.2` and removes merged local/origin feature branches at the user's request. Master remains the sole active branch; rollback tags retain historical checkpoints. Release preparation does not start Phase 5.

D60 completed: `v26.09-modded.2` / `26.09-modded.2` published from tagged master `c1660c34` with verified Windows x64 ZIP and checksum. Fresh Release suite passed 447/447. Only master remains locally/on origin; merged history and modded.1 rollback tag remain. Phase 5 is not started.

D62/D63 CargoDist implementation includes native settings and grouped station/vehicle views, line capacity, cargo overlays, destination checks and companion persistence. Final Debug/Release suites each passed 514/514 with 67 CargoDist checks. Native acceptance and build caveats are in development status. Consists/shunting and the new save container remain deferred; no master merge or new release is implied.


D64 follows the user's truck-placement assertion on CargoDist. Correct centre-line-to-lane conversion and checked native road reversal cover road vehicles and trams; six new placement regressions passed in the 10/10 focused Debug suite. Final full-suite results and the user placement recheck are recorded in development status. This is a stability follow-up on `codex/cargo-distribution`, not a later milestone or a master/release change.


D64 final builds and complete suites pass 520/520 in Debug/Release on `22d67d05` (10 placement, 67 CargoDist). The user confirmed the crashing placement was near a bend/junction; the remaining task is the same placement with the new executable. Keep TESTING; no merge or release/tag change.

### D65 automatic normal-route transfers

Supersede the standalone-window/overlay milestone with automatic truck/combined-station/train handoffs through private/shared/possible-rule routes. Native Cargo tabs expand destination quantities inline; Options Misc offers Automatic/Classic while preserving mixed and advanced saved settings. Reached-stop occurrence guards, useful-hop boarding, through retention, restrictions, routing replacement and transient reload context are covered by focused regressions. Packed/companion versions remain unchanged. Build both configurations, run complete suites against 520 and record evidence in development status; next is the user's short coal/passenger/reload check. Keep TESTING, preserve master/releases, defer consists/shunting/save-container work.

D65 discovery refinement: changed-vehicle setup is bounded to 4096 work steps. Unfinished possible rides resume using the solver's 4096-step tick budget; the old complete service is retained until replacement. Adjacent load/unload-capable boundaries seed a sparse graph, while same-car through boarding can use its reachable destination directly. Pending contexts are transient and restart safely after load. Coverage includes the 63-order native limit with two cargo types, pending discovery save/reload, later-stop boarding, Group-wrapped unconditional unloading and pickup discarding the old occurrence. Focused Debug passes 89/89; final complete-suite results are in development status.

D65 final source `c54cfbc8` passes reconfigured game builds and complete suites **542/542 in both Debug and Release**, with 89 cargo tests (baseline 520 full). See development status for stamps/logs and test-only symbol warnings. Automated implementation is ready for the user-only short coal/passenger/save check; CargoDist remains TESTING. Master, release checkpoints and deferred phases remain unchanged.
