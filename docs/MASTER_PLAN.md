# Master plan

OpenLoco Advanced Modding Framework turns this fork into an extensible transport simulation while the unmodified game remains playable. Current gameplay features are native, opt-in systems using shared engine code (D23); no mod loader is shipped.

This file is the authority for goals and sequencing. Detailed behaviour lives in the linked documents. Those documents must not contradict this plan.

## Goals

- Keep Chris Sawyer's Locomotion graphics, camera, UI style, and default rules.
- Existing optional systems preserve default OpenLoco behaviour. D62 explicitly makes CargoDist automatic in new and older saves; selecting Manual preserves classic cargo distribution.
- Share one implementation of orders, reservations, cargo routing, and programmable logic across gameplay systems.
- Let players configure the delivered gameplay systems in native windows without compiling C++.
- Stay compatible with upstream OpenLoco where that does not block the framework. Track that in `UPSTREAM_TRACKING.md`.

## Non-goals for the current milestone

- No Lua, Wasm, text script runtime, or network download.
- No zip installer.
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

### C. Gameplay systems

Native opt-in features and later subsystem milestones:

1. Programmable logic
2. Train shunting
3. Path signals
4. Shared vehicle lines
5. Timetables
6. Passenger destinations
7. Cargo demand
8. Expanded object limits
9. Larger worlds

Object limits and world size are engine work exposed as capabilities. Their eventual persistence design must record the required capabilities; no package activation API is implemented. A normal data mod cannot replace `S5::Limits` or the map pitch by itself.

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
| Orders: End, StopAt, RouteThrough, RouteWaypoint, UnloadAll, WaitFor | Present. Three-bit type field. Block rules are implemented in TESTING and are not stored in that byte. |
| Block signals and a 64-entry routing ring | Present. Path and chain are extra flags on a normal signal. |
| Depot coupling, rearrange, reverse | Present. No on-track split or run-around. |
| Cargo as quantities with an origin station | Present. No passenger destination. |
| Map 384x384, pitch 512, fixed object and entity caps | Present and packed into the save |
| Multiplayer sockets and unused checksum fields | Present as incomplete code. UI control hidden. |
| Shared lines, timetables, path and chain signals | In the game. Companions beside the SV5 |
| NSF, NOF | Absent |
| Mod loader | Removed |

## Sequencing

Phases are detailed in `ROADMAP.md`. The short form:

0. Audit and these documents. Done for this baseline.
1. Mod loader. Removed (D23).
2. Packages and manager UI. Removed with the loader (D23).
3. Shared lines, then timetables.
4. Block rules on a vehicle's orders and on a shared line's orders, edited inline in their native order windows. Checkpoint before this step: `v26.09-modded.1`.
5. Versioned save container and mod state. Align with upstream NSF. DEFERRED; matching companions are permitted.
6. Dynamic consists and on-track coupling. DEFERRED until after CargoDist.
7. Path reservations and signals.
8. Shunting jobs and yard programs. DEFERRED until consists exist.
9. Cargo demand. CargoDist routing is implemented in TESTING ahead of Phases 5/6/8; industry markets remain later.
10. Passenger destinations. CargoDist packets/transfers are implemented in TESTING; new generation models remain later.
11. Object limits and larger worlds, aligned with NOF.
12. Performance, packaging, release.

Block rules save in a companion, as lines and signals already do, until the save container exists. Shunting gameplay waits for consists. Timetables already run without shunting. Shunting must work without passenger destinations.

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

## Current milestone (D62, 2026-10-14)

CargoDist is implemented in TESTING on `codex/cargo-distribution`. Shared lines/timetables, path/chain signals and the first logic delivery are COMPLETED by user acceptance and recorded 447/447 tests. The user deferred Phase 5, consists and shunting, and approved complete passenger/mail/freight distribution with automatic defaults for new and older saves. Manual distribution preserves classic behaviour when selected. Packed saves stay unchanged; a matching `.ocargodist` companion carries extended cargo state. See `mods/CARGO_DISTRIBUTION.md`.

## Historical milestone records

Phase 4 is implemented on `codex/block-logic-programming` and remains TESTING until its required in-game checks are recorded. Native opt-in features supersede the original package-loader goal (D23). Preserve master and `v26.09-modded.1` while testing; no release or merge is implied. Consists, shunting, destinations, NSF/NOF and multiplayer remain later work.

Phase 4 includes actual multi-row blocks in the vehicle Route, shared-line Orders and Rules windows; simultaneous current-program/next-destination feedback; Else/Else if; Forever repeats and Break. Native acceptance remains user-run before completing this milestone.

Rules uses MakeCode-style shapes inside native windows (D34), with explicit inside slots that grow route sections. Classic order lists without blocks keep their existing presentation. Latest Phase 4 fixes remain in TESTING for user manual recheck.

Phase 4 also distinguishes Before travelling decisions from physical On arrival decisions. Waits use normal movement/braking to reach their boundary before the clock starts; arrival choices stay fixed through cargo handling, and next-trip checks observe the vehicle after loading. Current future-stop facts support look-ahead choices without adding predictions. User acceptance now includes the Wait-only When control, waypoint braking and save/reload during approach (D36/D37).

Phase 4 also provides direct block-field editing through native selectors/value dialogs, palette clicks, branch Actions and optional Details in a compact 560x320 Rules window. Vehicle/shared-line field clicks focus the exact editor field. All changes use validated commands; menu/dialog state is transient and existing persistence versions remain unchanged (D39–D41). The latest automated suites pass, with native acceptance still user-run.

Phase 4 also keeps manual-Skip white markers visible and projects shared-line rule-selected paths live on the map. Fresh first-row rule evaluation and current facts supplied by the progress vehicle drive bounded, incrementally cached whole-line diagrams; actual simulation and save versions are unchanged (D46). Arrow/map appearance and responsiveness remain user-run acceptance items (D43/D44).

Phase 4 live map paths publish complete replacements atomically and keep stable lane membership and bounded All-view admission (D45). Separate vehicle-progress projections preserve the selected departure during loading; whole-line map diagrams use current conditions (D46). Actual vehicle execution is unchanged. The reported display issue has regression coverage and awaits user visual acceptance.

D46 corrects whole-line map semantics: only the currently selected branch contributes stops/waypoints and legs, including multi-row If/Else sections. Vehicle execution progress stays separate. Physical movement and advancing the order cursor no longer cause route rebuilds when facts are unchanged. Phase 4 remains TESTING for user manual visual acceptance.

D47 fixes intermittent disappearance during cache refreshes: topology invalidation retains published Path/All snapshots while rebuilding, and unrelated line metadata/rule changes avoid flushing world geometry. Explicit save/game resets still clear the display immediately. Current-condition route selection, actual vehicle execution and bounded work remain unchanged; Phase 4 remains TESTING for user acceptance.

Signal follow-up D48 is implemented in TESTING on `codex/directional-waypoint-signals`, based on the preserved Phase 4 branch. Path/chain/combined reservations require connectivity to the encoded waypoint and retain the order while blocked; unrelated free exits cannot substitute. Waiting posts display red read-only, with existing occupancy, bounded work, retries and save formats retained. Full Debug/Release suites each passed 377/377; final native acceptance remains user-run.

D49 closes the screenshot's free-circle workaround and native junction signal bypass on the same signal-fix branch. Required waypoint proofs cannot revisit the same directed junction before the waypoint, while distinct valid alternatives remain. Special signals on outgoing arms are checked before path mode starts. Both full suites passed 385/385; native acceptance remains user-run and TESTING.

D57 supersedes the separate-window UI workflow: the Rules button expands native Vehicle Route or shared-line Orders in place, retaining the line browser and existing controls. Compact C blocks use readable native medium-font fields and per-window edit sessions. This UI-only follow-up is on `codex/native-inline-rules`; Phase 4 remains TESTING pending the user's short appearance/edit/drag acceptance. No later gameplay milestone starts here.
