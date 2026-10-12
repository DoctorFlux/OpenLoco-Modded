# Testing plan

GoogleTest is the runner. `ctest --test-dir build/windows -C Debug` or `-C Release` executes it. New libraries register tests through `loco_add_library`.

The private upstream simulate/compare job needs repositories this fork does not have. Do not treat it as a local requirement.

Tests must not open the Steam Locomotion directory or `%APPDATA%\OpenLoco`.

## Existing coverage

136 tests passed in Release before the modding library existed. They cover core utilities, math, diagnostics, and `TileManagerTests`. They do not cover orders, signals, vehicles, saves, or mods.

## Required as each milestone lands

| Area | Tests |
| --- | --- |
| Manifest | Valid file, missing field, bad version, duplicate key, self-dependency, unicode escape, API rule |
| Dependencies | Order, missing dep, cycle, conflict, optional present, optional absent |
| Discovery | Directory of mods, single root, bad JSON, incompatible API, oversized file, symlink skipped |
| Packages | Install, upgrade, uninstall, corrupt zip, path escape rejected |
| Saves | SV5 import, new file written elsewhere, checksum, missing mod, migration |
| Lines | Two vehicles share one edit; a vehicle with no line keeps vanilla orders |
| Timetables | Early vehicle waits; late vehicle does not freeze the game |
| Logic | No blocks matches current order advance. A group of two stops and one waypoint is skipped or visited from a cargo check. Go to stays on the same list. Repeat is bounded. Two vehicles on one line share blocks and keep separate counts. Step budget, unavailable check, deterministic next order |
| Consists | Uncouple, couple either end, parked wagons remain, reverse |
| Reservations | Crossing paths conflict; parallel paths do not; release on cancel |
| Shunting | Scenarios below |
| Cargo and passengers | Classic mode unchanged; destination mode covered by its own tests |
| Limits | `TileManagerTests` updated only when map constants change, in the same change |

## In-game scenarios before a feature is complete

- One shunter sorts mixed wagons into two sidings.
- Two shunters work one yard without taking the same wagon.
- A locomotive runs around its train and the service id stays.
- A train pushes wagons into a siding and stops before an occupied piece.
- Two trains cross a junction on non-overlapping paths.
- A save is loaded while a shunt job is mid-move, after the save container exists.
- A freight train requests wagons and a shunter assembles them.
- Passengers transfer between two lines.

## Determinism

For logic, reservations, and jobs, run the same command list twice and compare the resulting state. Do not compare frame times.

## Status

On 2026-10-10, `ctest --test-dir build/windows -C Debug -R "ActivationTests|ManifestTests|DependenciesTests|DiscoveryTests"` passed 22 tests and failed 0. `OpenLoco.exe mods --mods_path` then logged `Hello World from openloco.sample.hello` and exited 0. Symlink skipping is implemented and not covered by an automated test. Zip loading is not implemented.
