# Upstream tracking

Signal-safety reference check, 2026-10-23 (D74): inspected the pinned `e71abfb3` native admission code and the [public upstream vehicle-head implementation](https://github.com/OpenLoco/OpenLoco/blob/master/src/OpenLoco/src/Vehicles/VehicleHead.cpp). Ordinary signal admission continues to use native block occupancy; standalone Chain layers exit lookahead onto that block rule. This is a local safety follow-up, not an upstream merge or baseline change. Read existing save/object tracking below before further format work.

Release documentation check, 2026-10-15 (D69): reread the [public upstream repository/README](https://github.com/OpenLoco/OpenLoco). It describes the open-source Locomotion engine, original-asset requirement, completed C++ reimplementation and multiplayer/increased-limits goals. Modded.3 credits that engine and compares fork additions against pinned 26.09 baseline `e71abfb3`; no claim of all current upstream changes or fresh code/API audit is made. No upstream merge is part of this release. The detailed code audit below remains dated 2026-10-10.

Checked 2026-10-10 against local commit `e71abfb3` and public OpenLoco sources.

| Item | Finding |
| --- | --- |
| Local commit | `e71abfb3` "Fix various vanilla issues that cause divergences (#4063)". `master`, `origin/master`, and `upstream/master` matched at audit time. |
| Project version | `26.09` in the root `CMakeLists.txt`. |
| README goals | Multiplayer, higher map and vehicle limits, and a new save format (NSF) before those limits can change. |
| NSF | Named, not implemented. No reader, writer, or spec in this tree. |
| NOF | Named in the 30 January 2026 development post by Duncan Frost: remove DAT limits, preferably more human readable, design still open. Old objects remain supported. Object Editor has started using variable-length lists internally and still exports DAT. No NOF loader here. |
| Field identification | The same post says save and object fields should be named before NSF/NOF work. |
| Multiplayer | Changelog records the title-screen toggle as hidden because multiplayer is not reimplemented. `NetworkBase`, sockets, and server types exist. `GameState::multiplayerChecksumA/B` are marked unused. `multiplayerSave` command slot is null. |
| Modding | No package format, manifest, or script API upstream. `plugin.dat` is the vanilla plugin-object file. |
| Objects shipped by CMake | `OpenGraphics` `objects.zip` v0.1.12 via FetchContent. These are project assets, not a mod API. |
| Pathfinding note | The v26.01 post says land pathfinding likely waits for NSF; water pathfinding might not. |

## Rules

- Re-read this file and the upstream README before save, object, map-limit, or network work.
- If upstream publishes an NSF spec, adapt `SAVE_FORMAT.md` instead of growing a rival container.
- If upstream publishes a mod format, read it before adding a loader. This tree does not have one.
- Do not merge upstream with a force push. Merge or rebase only on a feature branch when asked.

## Source of the January 2026 notes

https://openloco.io/news/2026/01/openloco-v26.01.html
