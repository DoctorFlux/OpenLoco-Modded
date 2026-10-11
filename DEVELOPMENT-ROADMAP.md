# Vanilla source map

This file maps the OpenLoco systems at `e71abfb3`. It is not the project plan. The plan, status, and feature specs are in `docs/`. If this file and `docs/` disagree about sequencing, `docs/ROADMAP.md` wins.

The current tree started as unmodified OpenLoco. The mod loader and Hello World sample have been removed.

Shared lines, timetables, conditional orders, path signals, locomotive run-around, and passenger destinations are not in the source. Vanilla orders, two-sided block signals, consist coupling, and an exact-match SV5/SC5 save are.

Any new field that must survive a save has to wait for a versioned save format. `importSaveToGameState` in `src/OpenLoco/src/S5/S5.cpp` rejects every file whose header version is not `kCurrentVersion` (`0x62262`). `S5::GameState` is a packed layout with `static_assert(sizeof(GameState) == 0x4A0644)` in `src/OpenLoco/include/OpenLoco/S5/S5GameState.h`. Changing an array size or inserting a field fails that check.

## Dependency overview

```
S5::Limits + Engine/World.hpp
        |
        v
GameState arrays  <-->  S5::GameState / S5 file (exact version 0x62262)
        |
        +-- orders[256000] -- OrderManager -- VehicleHead order offsets
        +-- routings[1000][64] -- RoutingManager -- Routing.cpp block occupancy
        +-- entities -- Vehicle consist -- rearrange / couple / reverse
StationCargoStats <-- buildings and industries
                  <-- VehicleHead load / unload
```

Orders, routings, and signal occupancy are separate at runtime, but they sit in one packed `S5::GameState`. A feature that only uses the existing entity list can be prototyped before the new save format. Anything that must be stored has to follow the save work.

## Suggested order

1. Versioned save format.
2. Larger maps and higher object limits.
3. Rail reservations, then path signals.
4. Conditional orders.
5. Shared lines and timetables.
6. On-track uncoupling and shunting, then locomotive run-around.
7. Passenger destinations and cargo demand.

Steps 3, 4, and 6 can be explored in memory before step 1. They cannot be saved until step 1 exists. Step 2 and step 7 need the new format before their data can grow.

## 1. Versioned save format

The on-disk format is S5 (`.SV5` / `.SC5`). There is one legacy layout conversion and no general version ladder.

| Role | Path |
| --- | --- |
| Header, magic `0x62300` | `src/OpenLoco/include/OpenLoco/S5/S5.h` |
| Load, save, version gate | `src/OpenLoco/src/S5/S5.cpp` |
| Packed game state | `src/OpenLoco/include/OpenLoco/S5/S5GameState.h`, `src/OpenLoco/src/S5/S5GameState.cpp` |
| Chunk codec | `src/OpenLoco/include/OpenLoco/S5/SawyerStream.h`, `src/OpenLoco/src/S5/SawyerStream.cpp` |
| Per-record import/export | `src/OpenLoco/src/S5/S5Company.cpp`, `S5Town.cpp`, `S5Industry.cpp`, `S5Station.cpp`, `S5Entity.cpp`, `S5TileElement.cpp`, `S5Message.cpp`, `S5Animation.cpp`, `S5Wave.cpp`, `S5Options.cpp`, `S5LabelFrame.cpp` |
| Compare tool | `src/OpenLoco/src/GameSaveCompare.cpp` |
| Autosave | `src/OpenLoco/src/Scenes/GameScene.cpp` |

`GameStateType2` (`sizeof` `0x49EA24`) is the only migration already present: `importGameStateType2` copies that older company layout into the current one. A next-generation format should keep reading `0x62262`, then add an explicit version that can grow. New chunks should stay out of the packed `0x4A0644` blob so vanilla saves still load.

Testing: round-trip a vanilla `.SV5` through load and save and compare it with `GameSaveCompare`. Add a fixture that loads `0x62262`, saves as the new version, and loads again. Reject unknown future versions with a clear error instead of the current blanket "Unsupported S5 version".

## 2. Larger maps and object limits

Map size and object counts are compile-time constants baked into the save layout.

| Constant | Value | Path |
| --- | --- | --- |
| `kMapColumns`, `kMapRows` | 384 | `src/Engine/include/OpenLoco/Engine/World.hpp` |
| `kMapPitch` | 512 | same file |
| `kTileSize` | 32 | same file |
| `kMaxElements` | `3 * kMapColumns * kMapRows` | `src/OpenLoco/include/OpenLoco/Map/TileManager.h` |
| Entity caps, towns, stations, vehicles | see file | `src/OpenLoco/include/OpenLoco/S5/Limits.h` |
| Alias | `using namespace S5::Limits` | `src/OpenLoco/include/OpenLoco/Engine/Limits.h` |
| Object pool | `kMaxObjects = 859` | `src/OpenLoco/include/OpenLoco/Objects/ObjectManager.h` |

Notable caps in `Limits.h`: companies 15, towns 80, industries 128, stations 1024, entities 20000, vehicles 1000, routings per vehicle 64, orders 256000, cargo objects 32, track objects 8, vehicle objects 224, train signals 16.

Consumers that assume 384 include `src/OpenLoco/src/Map/TileManager.cpp`, `src/OpenLoco/src/Ui/Windows/MapWindow.cpp`, `src/OpenLoco/src/Map/MapGenerator/MapGenerator.cpp`, `src/OpenLoco/src/World/Station.cpp` (cargo catchment grid), `src/OpenLoco/src/Entities/EntityManager.cpp` (spatial map sized from `kMapPitch`), and `src/OpenLoco/tests/TileManagerTests.cpp`.

Do this after the save format can store a different pitch and larger arrays. Tile elements are already a variable-length chunk. The index pitch and the game-state arrays are not.

Testing: extend `TileManagerTests` for the new pitch. Generate a small map and a map larger than 384 and confirm station catchment, the map window, and entity spatial lookup. Load an old 384 save into the new format.

## 3. Path signals and rail reservations

Signals are two-sided block signals. Occupancy is a bit on the signal plus a per-vehicle ring of at most 64 track pieces. There is no path-signal type and no separate reservation map.

| Role | Path |
| --- | --- |
| Signal tile | `src/OpenLoco/include/OpenLoco/Map/SignalElement.h` |
| Signal object | `src/OpenLoco/include/OpenLoco/Objects/TrainSignalObject.h` |
| Place and remove | `src/OpenLoco/src/GameCommands/Track/CreateSignal.cpp`, `RemoveSignal.cpp`, `CreateSignalsAuto.cpp`, `RemoveSignalsAuto.cpp` |
| Block search and occupancy | `src/OpenLoco/src/Vehicles/Routing.cpp` (`findSignalOnTrack`, `isBlockOccupied`) |
| Declarations | `src/OpenLoco/include/OpenLoco/Vehicles/Vehicle.h` |
| Reserved pieces | `src/OpenLoco/include/OpenLoco/Vehicles/Routing.h`, `RoutingManager.h`, `src/OpenLoco/src/Vehicles/RoutingManager.cpp` |
| Track connections | `src/OpenLoco/src/Map/Track/Track.cpp` |
| Movement that claims and releases | `src/OpenLoco/src/Vehicles/VehicleHead.cpp`, `VehicleTail.cpp`, `VehicleManager.cpp` |
| Pass signal at danger | `src/OpenLoco/src/GameCommands/Vehicles/VehiclePassSignal.cpp` |

`CompanyAiPathfinding.cpp` scores track the AI wants to build. It is not the train pathfinder. Keep path signals in `Routing.cpp` and `SignalElement`.

A train that must reserve more than 64 pieces needs a larger routing ring, which changes `GameState` and therefore depends on steps 1 and 2. A path signal can otherwise be a new signal-object flag plus a reservation check beside `isBlockOccupied`.

Testing: two trains approaching one junction, a train longer than the reserved look-ahead, and a signal that stays red until the tail releases the block. There are no signal tests today.

## 4. Conditional orders and programmable routes

The programmable route is one global order byte array. Each vehicle owns a slice. There is no if/then order.

| Role | Path |
| --- | --- |
| Order encoding | `src/OpenLoco/include/OpenLoco/Vehicles/Orders.h`, `src/OpenLoco/src/Vehicles/Orders.cpp` |
| Global table | `src/OpenLoco/include/OpenLoco/Vehicles/OrderManager.h`, `src/OpenLoco/src/Vehicles/OrderManager.cpp` |
| Per-vehicle offsets | `src/OpenLoco/include/OpenLoco/Vehicles/VehicleHead.h` |
| Runtime storage | `src/OpenLoco/include/OpenLoco/GameState.h` |
| Orders UI | `src/OpenLoco/src/Ui/Windows/Vehicle.cpp` |
| Commands | `src/OpenLoco/src/GameCommands/Vehicles/VehicleOrderInsert.cpp`, `VehicleOrderDelete.cpp`, `VehicleOrderUp.cpp`, `VehicleOrderDown.cpp`, `VehicleOrderSkip.cpp`, `VehicleOrderReverse.cpp` |
| Dispatch | `src/OpenLoco/src/GameCommands/GameCommands.cpp` |
| Movement | `src/OpenLoco/src/Vehicles/VehicleHead.cpp` |

`OrderType` is `End`, `StopAt`, `RouteThrough`, `RouteWaypoint`, `UnloadAll`, and `WaitFor`. `Order::getType()` keeps only three bits (`_type & 0x7`), so two more type values fit before the encoding breaks. `WaitFor` waits for a cargo type, not a clock time. `RouteThrough` is non-stop, not a shared line.

`kMaxOrders` is 256000. `kMaxOrdersPerVehicle` is 63 because `kMaxRoutingsPerVehicle` is 64.

Conditional orders need either one of the two spare type values or a wider encoding. A wider encoding changes the order blob and depends on step 1.

Testing: insert, delete, and skip orders; a condition that is true and one that is false; a full 63-order list. No order tests exist today.

## 5. Shared lines and timetables

These systems do not exist. Each vehicle has its own slice of `orders[]`. `OrderManager::reverseVehicleOrderTable` reverses that one vehicle's table when the train reverses. It does not share orders.

A shared line is new data that several vehicles can reference, instead of copying a slice. A timetable needs a time field. `OrderWaitFor` cannot carry one.

Depends on step 4 if a shared line is itself an order, and on step 1 because the reference must be saved.

Testing: two vehicles on one shared list, editing the list once and seeing both trains follow it, then a departure slot that holds a train until the scheduled time.

## 6. Coupling, uncoupling, shunting, reversal, and run-around

Coupling is a linked consist (head, body, bogies, tail), not a coupler that can be split in a yard. Reversal exists. A locomotive run-around does not.

| Role | Path |
| --- | --- |
| Consist editing | `src/OpenLoco/include/OpenLoco/Vehicles/Vehicle.h`, `src/OpenLoco/src/Vehicles/Vehicle.cpp` |
| Compatibility | `canVehiclesCouple` in `src/OpenLoco/src/Vehicles/VehicleHead.cpp` |
| Object flags | `src/OpenLoco/include/OpenLoco/Objects/VehicleObject.h` |
| Depot rearrange | `src/OpenLoco/src/GameCommands/Vehicles/VehicleRearrange.cpp` |
| Drag UI | `src/OpenLoco/src/Ui/Windows/DragVehiclePart.cpp` |
| Build and clone | `src/OpenLoco/src/GameCommands/Vehicles/CreateVehicle.cpp`, `CloneVehicle.cpp` |
| Player reverse | `src/OpenLoco/src/GameCommands/Vehicles/VehicleReverse.cpp` calls `VehicleHead::sub_4ADB47` |
| Reverse implementation | `sub_4ADB47` in `src/OpenLoco/src/Vehicles/VehicleHead.cpp` |
| Order reversal | `OrderManager::reverseVehicleOrderTable` |

`canVehiclesCouple` checks object compatibility. Depot rearrange already moves cars inside a depot. Splitting a train that is sitting on the track needs a new game command. Run-around is that split, plus the existing reverse path, plus coupling the locomotive onto the other end.

This can start in parallel with signals and orders if it only uses the existing entity list. Saving a consist that vanilla S5 cannot represent waits for step 1.

Testing: couple and uncouple in a depot (already possible), then the same on a plain track, then reverse, then detach the locomotive, run it past the train, and couple it to the tail. Confirm the order list is reversed exactly once.

## 7. Passenger destinations and cargo demand

Cargo is an anonymous quantity with an origin station, not a passenger with a destination. `StationCargoStats` in `src/OpenLoco/include/OpenLoco/World/Station.h` stores quantity, origin station, acceptance flags, age, rating, density, and the industry that produced it. `kMaxCargoStats` is 32. The packed S5 record is a different layout (`sizeof(S5::StationCargoStats) == 0xD` in `src/OpenLoco/include/OpenLoco/S5/S5Station.h`).

| Role | Path |
| --- | --- |
| Station cargo | `src/OpenLoco/src/World/Station.cpp` (`updateCargoAcceptance`, `updateCargo`, `updateCargoDistribution`) |
| Delivery | `src/OpenLoco/src/World/StationManager.cpp` |
| Building production | `src/OpenLoco/src/Map/BuildingElement.cpp` |
| Industry production | `src/OpenLoco/src/World/Industry.cpp` |
| Cargo object | `src/OpenLoco/include/OpenLoco/Objects/CargoObject.h` |
| Load and unload | `VehicleHead::updateLoadCargo` / `updateUnloadCargo` in `src/OpenLoco/src/Vehicles/VehicleHead.cpp` |
| Payments | `src/OpenLoco/src/Economy/Economy.cpp` |

Buildings and industries deliver a quantity into a station pile. Acceptance is a catchment bitmask. Destinations need a new identity on top of that pile, stored by the new save format.

Testing: a town that produces passengers, two stations that accept them, and a vehicle that is paid for delivery to the demanded station rather than the first accepting station. Keep the current anonymous-quantity path covered so vanilla cargo payments do not change until the new mode is on.

## Tests that exist today

Tests are GoogleTest. `OPENLOCO_BUILD_TESTS` defaults to on in the root `CMakeLists.txt`. `cmake/OpenLocoUtility.cmake` builds `${TARGET}Tests`, links `GTest::gtest_main`, and registers tests with `gtest_discover_tests`.

| Target | Tests |
| --- | --- |
| `OpenLocoTests` | `src/OpenLoco/tests/TileManagerTests.cpp` |
| `CoreTests` | EnumFlags, FileStream, MemoryStream, Numerics, Prng, Store, Timer, Traits |
| `MathTests` | Vector, Trigonometry |
| `UtilityTests` | LookupTable, String |
| `DiagnosticsTests` | Logging, Assertion |

`OpenLoco.exe` is the `App` target (`src/App/CMakeLists.txt`, entry `src/App/src/Main.cpp`). No test covers orders, signals, vehicles, cargo, or S5. `TileManagerTests.cpp` assumes the current map constants, so it is the first test to update when the map size changes.

Run the suite with:

```
ctest --test-dir build/windows -C Release --output-on-failure --parallel
```

Upstream CI also runs a private simulate/compare job against OpenLoco's Locomotion asset and test-data repositories. Those repositories are not part of this checkout.
