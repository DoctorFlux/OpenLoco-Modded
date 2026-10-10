# Development status

States: `NOT STARTED`, `RESEARCHING`, `PLANNED`, `IN DEVELOPMENT`, `BLOCKED`, `TESTING`, `COMPLETED`.

`COMPLETED` requires a recorded test or in-game check. A successful compile is not enough.

Baseline: OpenLoco `e71abfb3`, branch `dev/local-baseline`. Vanilla behaviour is the upstream game, not a framework feature.

## Status

| Feature | Spec | Implementation | Engine dependency | Mod dependency | UI | Save/load | Tests | Issues | Next step |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Repository audit and plan | COMPLETED | COMPLETED | None | None | n/a | n/a | Build verified earlier; see changelog | None | Keep status current |
| Local Windows toolchain | COMPLETED | COMPLETED | VS 2022, CMake preset `windows` | None | n/a | n/a | 136 Release tests passed before this work | MFC not installed and not required | Use `README-LOCAL-SETUP.md` |
| Mod manifest parser | COMPLETED | TESTING | `src/Modding` only | None | None | Not saved | 22 Debug tests passed, 0 failed (`ctest -C Debug -R ActivationTests\|ManifestTests\|DependenciesTests\|DiscoveryTests`) | Not a general JSON library | Keep the schema closed |
| Dependency resolver | COMPLETED | TESTING | Parser | None | None | Not saved | Included in those 22 tests | A conflict is reported for each manifest that lists it | None |
| Directory discovery | COMPLETED | TESTING | Parser, resolver | None | None | Not saved | Included in those 22 tests | No zip reading. Symlinks are skipped and not yet covered by a test | Add a symlink test where the OS allows it |
| Mod activation | COMPLETED | TESTING | Discovery plus `enabled.json` | None | None | Not saved | `ActivationTests` (4) and `OpenLoco.exe mods --mods_path` logged the sample line, exit 0 | Only the `log` action exists. Enablement is not stored in a save | Mod manager UI |
| `.olmod` archives | PLANNED | NOT STARTED | Archive library choice | Manifest | None | None | None | libzip is not wired in CMake here | Choose a library after a fresh check |
| Mod manager UI | PLANNED | NOT STARTED | Loader plus UI extension | None | NOT STARTED | NOT STARTED | None | No window type reserved | After packages |
| Sample Hello World | COMPLETED | TESTING | Activation | None | Log line only | Not saved | Executable run on 2026-10-10 printed `openloco.sample.hello: Hello World from openloco.sample.hello`. `openloco.yml` was unchanged | Must be listed in `enabled.json`. No gameplay effect | Copy it into `%APPDATA%\OpenLoco\mods` to try it in a normal launch |
| Programmable logic | PLANNED | NOT STARTED | Events, commands, step budget | Loader | NOT STARTED | Blocked on save container | None | No interpreter | Spec is `docs/mods/PROGRAMMABLE_LOGIC.md` |
| Shared lines | PLANNED | NOT STARTED | Order table or a new line store | Loader | NOT STARTED | Blocked on save container | None | Orders are per vehicle today | In-memory line model |
| Timetables | PLANNED | NOT STARTED | Shared lines, tick time | Lines | NOT STARTED | Blocked on save container | None | `WaitFor` is cargo, not time | After lines |
| Path signals | PLANNED | NOT STARTED | Reservation map | Reservations | NOT STARTED | Blocked on save container | None | Only block signals exist | Design reservation keys |
| Shunting | PLANNED | NOT STARTED | Consists, reservations, logic | Those three | NOT STARTED | Blocked on save container | None | Coupling is depot-oriented | Do not start gameplay yet |
| Cargo demand | PLANNED | NOT STARTED | Cargo routing | Optional shunting | NOT STARTED | Blocked on save container | None | Cargo is anonymous quantity plus origin | Keep classic mode |
| Passenger destinations | PLANNED | NOT STARTED | Aggregated groups | Lines optional | NOT STARTED | Blocked on save container | None | No destination field | Keep classic mode |
| Expanded object limits | RESEARCHING | NOT STARTED | Save container and object records | Capability flag | NOT STARTED | Blocked | None | Caps are compile-time and packed | Wait for NOF notes |
| Larger worlds | RESEARCHING | NOT STARTED | Coordinates, tiles, save container | Capability flag | NOT STARTED | Blocked | `TileManagerTests` assume 384 | Pitch is 512 | Do not change constants yet |
| NSF / NOF | RESEARCHING | NOT STARTED | Upstream has no code here | n/a | n/a | SV5 only | None | Plans described in the v26.01 post | Re-check before design changes |
| Multiplayer | RESEARCHING | NOT STARTED | Command stream, mod version check | All simulation mods | Hidden upstream | Checksums unused | None | Not reimplemented upstream | Do not claim support |

## Changelog

- 2026-10-10: Audited `e71abfb3`. Added manifest parsing, dependency resolution, directory discovery, and log-only activation. Debug `OpenLoco.exe mods --mods_path` activated `mods/examples/hello-world` and logged its line. 22 Debug mod tests passed. `openloco.yml` was not modified. Vanilla simulation code was not changed.

## Next actionable task

Add a mod-manager window that lists discovered mods and edits `enabled.json`. Do not store that list inside an SV5 save. Zip packages stay blocked until an archive library already used by the build is confirmed.

## Blockers

- Persistent mod state cannot be added inside `S5::GameState` without breaking the exact version check.
- Shunting and path signals need engine services that do not exist yet.
- Script language is undecided on purpose. See `DECISIONS.md`.
