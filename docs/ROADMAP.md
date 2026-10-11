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
