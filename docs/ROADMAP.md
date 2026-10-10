# Roadmap

Each milestone is independently buildable. Do not land a whole phase as one change. Status values are defined in `DEVELOPMENT_STATUS.md`.

## Phase 0 — Baseline

Done for commit `e71abfb3`.

- Local Windows build, tests, and launch notes: `README-LOCAL-SETUP.md`.
- Vanilla source map: `DEVELOPMENT-ROADMAP.md`.
- This documentation set.

## Phase 1 — Mod loader skeleton

Implemented through activation. Still not a mod manager.

1. Parse `manifest.json`. Implemented.
2. Resolve dependencies, optional dependencies, conflicts, and cycles. Implemented.
3. Discover an unpacked mod directory and enforce API compatibility. Implemented.
4. Sample and template packages under `mods/`. Implemented.
5. `OpenLoco.exe mods --mods_path <dir>` validates and activates mods listed in `enabled.json`, then exits. A normal launch does the same for `%APPDATA%\OpenLoco\mods` when that directory already exists. Implemented.
6. The only action is `scripts/activate.json` with a `log` array. Persist enablement in the save container later; `enabled.json` is the install switch and is not a save.

## Phase 2 — Packages and manager

1. Define zip layout for `.olmod` using an existing archive library if one is already linked. libzip is named in the README and is not referenced by `CMakeLists.txt` in this tree. Re-check before choosing.
2. Install, upgrade, and remove unpacked mods in a user directory that is not the Steam folder.
3. Mod manager window: list, enable, disable, errors, dependencies.
4. Refuse activation when the API, capability, or conflict check fails.
5. Per-save required-mod list once the save container exists. Until then, show "not stored in saves".

## Phase 3 — Shared lines and timetables

1. Line record: id, name, mode, station list, shared order slice.
2. Assign and remove vehicles without copying orders per vehicle.
3. Keep vanilla orders as the fallback when a vehicle has no line.
4. Timetable rows: dwell, departure target, interval.
5. Hold an early vehicle. Recover a late one by skipping dwell, not by teleporting.
6. UI for lines and timetables.
7. Tests that do not require shunting.

Specs: `docs/mods/SHARED_LINES.md`, `docs/mods/TIMETABLES.md`.

## Phase 4 — Programmable logic

1. Instruction set and resource limits in `docs/mods/PROGRAMMABLE_LOGIC.md`.
2. Interpreter with step budget, cancellation, and a trace.
3. Save programs only after phase 5. Until then, tests load programs from memory.
4. Visual editor using native widgets. Text form is a projection of the same graph, not a second language.
5. Wire conditions that already exist (cargo waiting, date) before conditions that need new engine data.

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

1. Reservation map keyed by track piece, direction, and owner.
2. Conflict test for junctions, crossovers, parallel tracks, and pushed wagons.
3. Path signals as an extra signal mode. Block signals stay the default.
4. Deadlock detection that stops and reports instead of freezing.

## Phase 8 — Shunting

1. Job records and state machine.
2. Single shunter sorts wagons.
3. Two shunters in one yard.
4. Run-around and push into a siding.
5. Yard program and shunter program on the shared interpreter.
6. Freight requests only if cargo demand is also loaded.

## Phase 9 — Cargo demand

Destination-aware freight with classic mode left on by default. Spec: `docs/mods/CARGO_DEMAND.md`.

## Phase 10 — Passengers

Aggregated origin-destination groups and transfers. Classic passengers remain the default. Spec: `docs/mods/PASSENGER_DESTINATIONS.md`.

## Phase 11 — Objects and worlds

After NSF/NOF direction is rechecked: raise caps, variable map sizes, generation controls. Specs: `docs/mods/EXPANDED_OBJECT_LIMITS.md`, `docs/mods/LARGER_WORLDS.md`, `ENGINE_LIMITS.md`.

## Phase 12 — Release

Profile large maps, package `.olmod` files, write install instructions, and keep original assets out of the artifacts.

## Parallel work that is safe now

- Documentation and loader tests.
- Pure interpreter tests that do not touch `GameState`.
- Line and timetable data-model tests in memory.
- Upstream tracking when OpenLoco publishes NSF or NOF notes.

Not safe in parallel: editing `S5::GameState`, changing map constants, or hooking the loader into the title screen.
