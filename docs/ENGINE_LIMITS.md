# Engine limits

These are the caps compiled into this tree. They are not goals. Changing them changes `S5::GameState` and breaks the save check. Do not edit them in a drive-by patch.

## World

| Constant | Value | File |
| --- | --- | --- |
| `kMapColumns`, `kMapRows` | 384 | `src/Engine/include/OpenLoco/Engine/World.hpp` |
| `kMapPitch` | 512 | same |
| `kTileSize` | 32 | same |
| `kMaxElements` | `3 * columns * rows` | `src/OpenLoco/include/OpenLoco/Map/TileManager.h` |

`TileManagerTests` assumes these sizes.

## Simulation caps

From `src/OpenLoco/include/OpenLoco/S5/Limits.h`:

| Cap | Value |
| --- | --- |
| Companies | 15 |
| Towns | 80 |
| Industries | 128 |
| Stations | 1024 |
| Entities | 20000 |
| Vehicles | 1000 |
| Routings per vehicle | 64 |
| Orders per vehicle | 63 |
| Orders | 256000 |
| Cargo objects | 32 |
| Track objects | 8 |
| Vehicle objects | 224 |
| Train signal objects | 16 |

`kMaxObjects` is 859 in `ObjectManager.h`. The save stores `requiredObjects` at that size.

## Identifier width

Order types use 3 bits (`Order::_type & 0x7`). Two spare values remain. A wider order does not fit the current table.

Vehicle and station references in the vanilla structs are the existing id types. They are not a substitute for persistent mod references.

## Target candidates

Larger worlds to investigate later, not current support: 512, 1024, and 2048 tiles on a side. Memory, pathfinding, minimap, and generation have to be redesigned, not retargeted by a constant. See `docs/mods/LARGER_WORLDS.md`.

Object caps should become dynamic only when the object file format can express them. See `docs/mods/EXPANDED_OBJECT_LIMITS.md`.

## Performance bounds for new systems

- Logic programs: a fixed instruction budget per tick, with a wait state when exhausted.
- Reservations: lookup by track piece, not a scan of every train, once the map exists.
- Passenger groups: aggregated counts per origin, destination, and line, not one agent per person.
- Job scheduler: a bounded number of state transitions per tick.
