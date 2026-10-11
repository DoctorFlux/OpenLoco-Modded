# Master plan

OpenLoco Advanced Modding Framework turns this fork into an extensible transport simulation while the unmodified game remains playable. Major features ship as separately installable mods on a shared engine and mod API.

This file is the authority for goals and sequencing. Detailed behaviour lives in the linked documents. Those documents must not contradict this plan.

## Goals

- Keep Chris Sawyer's Locomotion graphics, camera, UI style, and default rules.
- Make new systems opt-in so a player who installs nothing gets the current OpenLoco game.
- Share one implementation of orders, reservations, cargo routing, and programmable logic across mods.
- Let players install a mod without compiling C++.
- Stay compatible with upstream OpenLoco where that does not block the framework. Track that in `UPSTREAM_TRACKING.md`.

## Non-goals for the current milestone

- No new gameplay in `OpenLoco.exe`.
- No in-game mod window.
- No zip installer, script runtime, or network download.
- No NSF/NOF implementation and no edit to `S5::GameState`.
- No claim that multiplayer works.

## Layers

### A. Engine foundation

General services the game and mods both need:

- Simulation state and entity identity
- Dynamic consists and vehicle commands
- Movement, pathfinding, occupancy, reservations
- Object loading and world coordinates
- Versioned serialization and migration
- UI extension points
- Event and command infrastructure

Vanilla code stays the implementation of today's game. New services are added beside it and adopted when a mod, or an opt-in engine option, needs them.

### B. Modding framework

The mod loader and Hello World sample were removed. Shared lines, timetables, and path and chain signals are in the game. A normal launch does not read a mods directory.

### C. Gameplay mods

Independent packages, not compile-time features of the executable:

1. Programmable logic
2. Train shunting
3. Path signals
4. Shared vehicle lines
5. Timetables
6. Passenger destinations
7. Cargo demand
8. Expanded object limits
9. Larger worlds

Object limits and world size are engine work exposed as capabilities. They are still packaged and versioned so a save can record that it needs them. A normal data mod cannot replace `S5::Limits` or the map pitch by itself.

## Principles

- Mutations go through validated commands that can later be replicated.
- New simulation uses the game tick and PRNG, not wall-clock time.
- Programs and jobs are bounded in steps and memory.
- Physical trains occupy real track. Shunting does not teleport.
- Classic passenger and freight behaviour remains available.
- SV5/SC5 files import. Extended saves are new files. Original saves are never overwritten.
- Native DLL mods are off. Scripts cannot touch the filesystem, processes, or network.
- Original Locomotion assets are never committed or shipped in a mod.

## What exists today

Audited at `e71abfb3`. Source map: `DEVELOPMENT-ROADMAP.md`.

| Area | State |
| --- | --- |
| C++ reimplementation, SDL3, OpenAL, SV5/SC5 | Present |
| Orders: End, StopAt, RouteThrough, RouteWaypoint, UnloadAll, WaitFor | Present. Three-bit type field. |
| Block signals and a 64-entry routing ring | Present. Not path signals. |
| Depot coupling, rearrange, reverse | Present. No on-track split or run-around. |
| Cargo as quantities with an origin station | Present. No passenger destination. |
| Map 384x384, pitch 512, fixed object and entity caps | Present and packed into the save |
| Multiplayer sockets and unused checksum fields | Present as incomplete code. UI control hidden. |
| NSF, NOF, mod packages, shared lines, timetables | Absent |
| Mod manifest parser, discovery, and log-only activation | Implemented. Hello World logs one line when enabled. |

## Sequencing

Phases are detailed in `ROADMAP.md`. The short form:

0. Audit and these documents. Done for this baseline.
1. Mod manifest, discovery, dependency checks. Started.
2. Packages and manager UI. The sample mod can be activated from the command line. The manager window is not started.
3. Shared lines, then timetables.
4. One programmable-logic interpreter and a visual editor.
5. Versioned save container and mod state. Align with upstream NSF.
6. Dynamic consists and on-track coupling.
7. Path reservations and signals.
8. Shunting jobs and yard programs.
9. Cargo demand.
10. Passenger destinations.
11. Object limits and larger worlds, aligned with NOF.
12. Performance, packaging, release.

Lines and logic can be designed before the save container, but they cannot be saved until phase 5. Shunting gameplay waits for consists and reservations. Timetables must work without shunting. Shunting must work without passenger destinations.

## Repository layout

- Engine and framework: this repository.
- Gameplay that is already in the game is documented under `docs/mods/`.
- Feature branches come off the current development branch. Do not rewrite `master`.

The packaging decision is in `DECISIONS.md`: hybrid monorepo until a mod needs its own release cadence.

## Related documents

- `ARCHITECTURE.md`, `SAVE_FORMAT.md`, `ENGINE_LIMITS.md`
- `DEPENDENCIES.md`, `MOD_COMPATIBILITY.md`, `FEATURE_MATRIX.md`
- `UI_UX_PLAN.md`, `TESTING_PLAN.md`, `ROADMAP.md`, `DEVELOPMENT_STATUS.md`
- `DECISIONS.md`, `UPSTREAM_TRACKING.md`
- `docs/mods/` for each gameplay spec
