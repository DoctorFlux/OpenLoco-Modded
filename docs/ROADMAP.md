# Roadmap

Each milestone is independently buildable. Do not land a whole phase as one change. Status values are defined in `DEVELOPMENT_STATUS.md`.

## Phase 0 — Baseline

Done for commit `e71abfb3`.

- Local Windows build, tests, and launch notes: `README-LOCAL-SETUP.md`.
- Vanilla source map: `DEVELOPMENT-ROADMAP.md`.
- This documentation set.

## Phase 1 — Mod loader

Removed. Shared lines, timetables, and path and chain signals are part of the game. There is no manifest loader, Mods window, or Hello World sample.

## Phase 3 — Shared lines and timetables

Implemented for the line record, assignment, vanilla order types, save companion, line manager window, per-stop departure and platform data, and timetable decisions. Movement of a vehicle on a line advances through that line and reverses at the end of a point-to-point line. Platform choices are stored; they are not yet forced onto the track pathfinder.

Specs: `docs/mods/SHARED_LINES.md`, `docs/mods/TIMETABLES.md`.

## Phase 4 — Block rules

Status: **TESTING** on `codex/block-logic-programming`. Model, runtime, native editor and v1 persistence are implemented. Finish the user-run manual acceptance checks before proceeding to Phase 5. The normal order tabs now show actual nested block headers/brackets, current program plus next destination and conditional colours; blocks resize across multiple rows, and both normal lists accept validated order dragging. Explicit inside slots grow containers and nested grips follow their visible position; the shared renderer now draws MakeCode-style puzzle shapes and sockets. Release `v26.09-modded.1` is the checkpoint before this phase. Spec: `docs/mods/PROGRAMMABLE_LOGIC.md`.

The phase is done when a vehicle and a shared line can both run the blocks, including a group of stops and waypoints, from the Rules window. A runner with no window, or a window that only edits one of the two lists, is not this phase.

Arrival timing is implemented: only Wait exposes When > Before travelling/On arrival; ordinary new blocks use automatic timing, and existing saved choices remain honored; waits finish native physical braking at their stop/waypoint before arming, arrival checks latch before cargo transfers, and next-trip checks run after loading. Current future-stop facts can drive advance skips/diversions. The optional v1 PHASES extension resumes approaches and arrival decisions. Next concrete task remains user manual acceptance, now including braking/arrival timing and save/reload during approach; do not proceed to Phase 5 yet.

The route-arrow fix uses a shared read-only committed travel/hold key in all three views, with a visible outlined white marker on the nested order row (D38). Recheck it during Skip/Go to, Repeat, waits and cargo handling in the user's next manual run; the program marker and provisional next-destination footer serve their separate purposes.

Phase 4 editing now includes clickable fields in all three block views, native value dialogs/dropdowns, paged long selectors, palette clicks and Else/Else-if/removal Actions. Rules defaults to 560x320 with optional Details and the same 360x240 minimum (D39–D41). Final full Debug and Release suites passed 329/329 each. Next concrete task is the user's short direct-editing/compact-layout check followed by the outstanding manual acceptance rows; Phase 4 remains TESTING.

Manual Skip now retains the arrow immediately in Vehicle/Rules (D43). Shared-line Path/All now projects current rule choices without changing simulation, with periodic selected-vehicle fact refresh, one missing leg search per UI update and a bounded geometry cache (D44). The latest full Debug/Release suites each passed 349/349. Next concrete task adds user manual arrow/live-map responsiveness checks to the remaining Phase 4 acceptance rows.

Path/All condition display now swaps complete per-line snapshots, stabilizes lanes and bounded All admission, and includes an already-selected loading departure (D45). Actual simulation is unchanged. Final Debug/Release suites passed 357/357 each. Next concrete task is the user's short visual condition-change recheck; Phase 4 remains TESTING.

Phase 4 follow-up D46 separates whole-line Path/All diagrams from copied vehicle execution progress. It evaluates the first row with fresh state, omits inactive If/Else stops and waypoints entirely, and removes moving-origin legs. Current facts still refresh live through the bounded cache/publication path. Full Debug/Release suites each passed 360/360. Next concrete task: user recheck the stop 1/waypoint 2 versus Else waypoint 3 example with waypoint 4 after the branch, then finish remaining manual acceptance. Keep TESTING.

D47 follow-up prevents completed Path/All routes disappearing during construction/cache refreshes, separates metadata invalidation and isolates hidden-line changes. Save/game resets clear publications immediately. Next concrete task: user recheck the previously intermittent same-route disappearance in the rebuilt executable; keep Phase 4 TESTING.

1. Block list, checks the game can already answer, If/Else if/Else, Skip, Go to, finite/Forever Repeat, Break, and the 256-step budget. Tests load the blocks from memory.
2. Run those blocks on a vehicle's own orders, and run the same blocks on a shared line. Each vehicle on a line keeps its own repeat count and wait.
3. Rules window. Its own window, drag and drop inside it, placed so it does not cover the line window, the vehicle window, or schemes.
4. Save `<save>.SV5.orules` when the tests above pass. A missing file leaves the orders as they are. This does not wait for phase 5 and does not widen `S5::GameState`.
5. Passenger, freight-destination, and yard blocks stay out of the palette until those features exist. They use this same window.

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

## Phase 8 — Shunting

1. Job records and state machine.
2. Single shunter sorts wagons.
3. Two shunters in one yard.
4. Run-around and push into a siding.
5. Yard rules and shunter rules in the same Rules window.
6. Freight requests only if cargo demand is also loaded.

## Phase 9 — Cargo demand

Destination-aware freight with classic mode left on by default. Spec: `docs/mods/CARGO_DEMAND.md`.

## Phase 10 — Passengers

Aggregated origin-destination groups and transfers. Classic passengers remain the default. Spec: `docs/mods/PASSENGER_DESTINATIONS.md`.

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
