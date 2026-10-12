# Architecture

## Layers and dependencies

```
OpenLoco.exe and vanilla libraries
        |
        v
Engine services (commands, entities, map, objects, serialization, UI hooks)
```

Shared lines, timetables, and path and chain signals are in the game. There is no mod loader. A normal launch does not read a mods directory.

## Current engine map

| Concern | Location |
| --- | --- |
| Entry | `src/App/src/Main.cpp` |
| Simulation tick and `loadG1` | `src/OpenLoco/src/OpenLoco.cpp` |
| Orders | `src/OpenLoco/include/OpenLoco/Vehicles/Orders.h`, `src/OpenLoco/src/Vehicles/OrderManager.cpp` |
| Consist and reverse | `src/OpenLoco/src/Vehicles/Vehicle.cpp`, `VehicleHead.cpp`, `GameCommands/Vehicles/VehicleReverse.cpp` |
| Block signals | `src/OpenLoco/src/Vehicles/Routing.cpp`, `Map/SignalElement.h` |
| Cargo | `src/OpenLoco/include/OpenLoco/World/Station.h`, `src/OpenLoco/src/World/Station.cpp` |
| Commands | `src/OpenLoco/src/GameCommands/GameCommands.cpp` |
| Save | `src/OpenLoco/src/S5/S5.cpp`, `S5GameState.h` |
| Objects | `src/OpenLoco/include/OpenLoco/Objects/ObjectManager.h` |
| Limits | `src/OpenLoco/include/OpenLoco/S5/Limits.h`, `src/Engine/include/OpenLoco/Engine/World.hpp` |
| Network shell | `src/OpenLoco/include/OpenLoco/Network/NetworkBase.h` |

`plugin.dat` is the vanilla plugin-object file copied into `%APPDATA%\OpenLoco`. It is not a mod loader.

## Engine services to add

These are interfaces to grow. They are not classes in the tree today.

| Service | Responsibility | First consumer |
| --- | --- | --- |
| `ModRegistry` | Discovery, enablement, API and dependency checks | Mod manager |
| `CommandBus` | Validate and apply simulation commands in order | Every mod |
| `EventBus` | Deterministic game events (tick, arrival, couple, signal) | Logic, timetables |
| `ConsistModel` | Ordered vehicles, orientation, couplers, service id | Shunting |
| `ReservationMap` | Path occupancy beyond the 64-word routing ring | Signals, shunting |
| `LineModel` | Shared order list referenced by many vehicles | Shared lines |
| `LogicRuntime` | One bounded block runner for a vehicle order list and a shared-line order list. Yards use it later | Rules window |
| `SaveContainer` | Versioned chunks plus SV5 import | Every persistent feature |
| `UiExtension` | Register windows in the existing widget style | Mod manager and editors |

Each service gets its own types and tests before a mod calls it. A mod never reaches into `GameState` arrays directly.

## Commands

A command is a plain value: type, issuer, arguments, and a predictable result code. The existing `GameCommands` table is the vanilla set. New commands are added beside it once the save container can record them. UI submits commands. It does not edit entities.

Failure codes are part of the API: blocked, missing entity, incompatible vehicle, reservation conflict, timeout, cancelled, invalid block. Callers must handle them.

## Identity

Vanilla entity indexes are reused by the pool and are not stable across a removed vehicle. Persistent references in mods use an id allocated by the engine and stored in the new save container. Until that container exists, mods do not persist entity references.

## Determinism

New systems read `GameState` PRNG streams and the simulation tick. They do not call `std::rand` or read the system clock for decisions. Iteration order is explicit and stable. Floating point is avoided in simulation decisions.

Multiplayer later replicates the command stream and checks mod ids plus API versions before the session starts. `NetworkBase` is not that system. See `UPSTREAM_TRACKING.md`.

## UI

New windows use the existing widget and string tables so they match Locomotion. Advanced windows stay closed until the player opens them. Tooltips state the reason a command failed.

## Performance

No new per-tick full-map scan without a budget. Path searches, logic steps, and job scheduling have step limits. Profiling comes before optimization. Parallel drawing already exists upstream and is not a place to put simulation work.

## Build

Windows: `cmake --preset windows`, then `windows-debug` or `windows-release`. Tests: `ctest --test-dir build/windows -C <Config>`. Details: `README-LOCAL-SETUP.md`.

## Block rules (implemented, TESTING)

`Rules/Rules.cpp` owns pure validation, atomic keyed edits, compilation and the bounded runner. `RulesGame.cpp` binds owner references to Lines/private order tables, submits validated mutations, evaluates live checks, manages independent per-vehicle execution and writes v1 companions. `Platforms.cpp` groups connected station track pieces and reads physical occupation, including wagons and excluding self. `Editor.cpp` supplies geometry; `RulesWindow.cpp` renders native widgets and submits edits. Native scroll release is separate from map-tool input.

Executable blocks carry Automatic/Before travelling/On arrival timing. Arrival gates select an anchor for physical travel, then evaluate only after the host records arrival; the entry row is consumed once. VehicleHead is the land route-planning head, while Vehicle2 is the physical front. Planner `passed` progression must not mark physical arrival. Rule approach stops extend no further routing; native Vehicle1/Vehicle2 distance braking brings the front to the head's routing handle/subposition before `finishApproach` arms a wait. Normal station arrivals evaluate through an arrival-only phase that defers future-trip decisions until cargo handling completes. The shared per-vehicle runner preserves approach, reached-entry and budget state in the optional v1 PHASES extension. Rules UI reads this state and changes timing only through validated edits.

Existing insert/delete/move/reverse/station-removal commands preserve stable keys and group references. Vehicle station/cargo/route-through/waypoint boundaries call the runner; loading continues during station waits. Assignment, detachment, clone and deletion update definitions and reset or clear execution as appropriate. A list without blocks uses existing vanilla progression. See `mods/PROGRAMMABLE_LOGIC.md` for limits and persistence, and `DEVELOPMENT_STATUS.md` for verification.

The Editor helpers now share read-only execution feedback, conditional colours, normal-list row geometry and cancellable drag proposals. `runtimeForVehicle` exposes a const runtime only when owner and revision match. Window code does not re-evaluate checks. Resizing a branch propagates its section to attached actions and paired Else/Else if before validating. Normal vehicle/line order tabs use the same helpers and submit commands; block-header clicks focus the exact block in Rules. One variable layout provides wrapped headers, C-shaped brackets, row coordinates and hit testing for all three views. Separate read-only program and next-stop status avoids hiding execution while waiting. OrderManager caches the displayed vehicle and regenerates active number frames after successful structural commands.

`ProgramVisual.closing` represents each container's interior append slot and closing arm. `programDrop` resolves its destination boundary and stable container key; `orderGripHit` uses the renderer's indentation. `Edit.into` carries explicit membership intent for insert/move commands. The model grows affected sections and transitive paired actions/clauses on a copy before validation and installation. Native shape helpers share notches, C cavities, boolean/value sockets and category colours across all views. Shape-only rendering supports software-raster tests without original font assets.

`FieldVisual` carries each field's kind, primary/secondary operand identity, label and bounds. `programLayout` is the single source for wrapped drawing and `blockFieldHit` in all three views. `fieldTextValue` returns full dialog values; `fieldTextEdit` prepares and validates a copied typed update without mutation. Rules callbacks additionally check owner, block key and revision against a fresh snapshot before `submit`. Native dropdown input ownership is set to the Rules canvas for inline fields; existing map-tool state and native scroll drop handling are retained. Long menus page within the native 32-bit disabled-mask capacity. Details and field-focus state are UI-only. `submit` validates unchanged proposals but avoids reinstalling orders or resetting live runtime when rows/blocks/key allocation are identical.

`routeOrderForVehicle` now resolves a fallback cursor against stable rows, including an optional displayed snapshot for plain Rules lists. Manual Skip sets `Runtime.current` at its existing entry before later evaluation; the marker lookup remains read-only. `previewRoute` uses a Runtime copy and one compiled instruction stream, enforcing 256 steps/64 destinations. `previewLineRoute` binds read-only live facts, memoizes repeated checks and permits station/year queries without an assigned vehicle. Copy-only arrival gates allow route projection without physical arrival mutation.

LinePath keeps static no-block routes separate from live layers. Selected facts refresh every eight UI updates and one other visible rule line every two updates; one missing leg per update uses the shared mode-specific search. Directed endpoint/quantized-origin keys reuse geometry across changed choices. LRU caps are 128 legs/131072 combined entries, with 32768 points/pieces per leg. Published per-line snapshots replace atomically only when the requested geometry is ready (or explicitly limited), and visible layers compose only after publication; piece deduplication and break flags prevent duplicate or fictitious joins. Drawing and colour queries use cached layers. Native successful non-ghost topology commands invalidate geometry; rule commands invalidate plans. Clearing Rules and closing the view invalidate/clear stale state. All preview/cache state is transient and never participates in simulation decisions or save files. Published snapshots have their own 128-leg/131072-entry budget; staging-cache eviction does not remove visible geometry.
