# Architecture

## Layers and dependencies

```
OpenLoco.exe and vanilla libraries
        |
        v
Engine services (commands, entities, map, objects, serialization, UI hooks)
        |
        v
src/Modding  (manifests, discovery, dependencies, enabled.json, log actions)
        |
        v
mods/  (manifest, data, scripts, assets that are not original Locomotion files)
```

`src/Modding` may depend on `Core` and the C++ standard library. It must not depend on `OpenLoco` gameplay types. The executable calls `activateMods`. Gameplay libraries must not grow a dependency on a particular mod.

`OpenLoco.exe mods --mods_path <dir>` activates that directory and exits. A normal launch activates `%APPDATA%\OpenLoco\mods` only when the directory already exists. Unlisted mods are not run.

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
| Mod manifests | `src/Modding` |

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
| `LogicRuntime` | One bounded interpreter | Lines, yards, shunters |
| `SaveContainer` | Versioned chunks plus SV5 import | Every persistent feature |
| `UiExtension` | Register windows in the existing widget style | Mod manager and editors |

Each service gets its own types and tests before a mod calls it. A mod never reaches into `GameState` arrays directly.

## Commands

A command is a plain value: type, issuer, arguments, and a predictable result code. The existing `GameCommands` table is the vanilla set. New commands are added beside it once the save container can record them. UI and scripts submit commands. They do not edit entities.

Failure codes are part of the API: blocked, missing entity, incompatible vehicle, reservation conflict, timeout, cancelled, invalid program. Callers must handle them.

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
