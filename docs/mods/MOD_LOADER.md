# Mod loader

## Purpose

Find installed mods, validate them, and activate the ids listed in `enabled.json`. `OpenLoco::Modding::activateMods` does this. The game command is `OpenLoco.exe mods --mods_path <directory>`.

## Player-facing behaviour

- Mods live in a user folder that is not the Steam Locomotion directory.
- A mod is a directory with `manifest.json`, later a `.olmod` zip of that directory.
- Enable and disable are explicit. Installing a file does not activate it inside a running save.
- The manager lists name, id, version, API requirement, dependencies, conflicts, and the last error.
- Missing dependencies and incompatible API versions are errors, not warnings that load anyway.
- Upgrade and uninstall do not delete save chunks. Discarding state writes a new save.
- Logs from the loader are plain text and do not include the player's Steam path unless the user selected that folder.

## Engine contract

- Parse and resolve with the rules in `MODDING_API.md`.
- Execute entry points only when the mod id is listed in `enabled.json`.
- The only accepted entry file is a JSON object `{"log":["..."]}` inside the mod directory. Paths containing `..` are rejected.
- Do not load DLLs.
- Capability strings are stored now and enforced before the first gameplay mod activates.
- Platform mismatch excludes the mod and records an error.

## Current code

| Piece | File |
| --- | --- |
| Manifest parse and API rule | `src/Modding/src/Manifest.cpp` |
| Dependency graph | `src/Modding/src/Dependencies.cpp` |
| Directory walk | `src/Modding/src/Discovery.cpp` |
| Activation | `src/Modding/src/Activation.cpp` |
| Game command | `src/App/src/Main.cpp` (`mods`) |
| Example | `mods/examples/hello-world` |
| Template | `mods/templates/basic` |

## Not done

Zip install, mod-manager UI, per-save requirements, remote repositories, and any action other than `log`.

## Tests

`ManifestTests`, `DependenciesTests`, `DiscoveryTests`. They use temporary directories, not the game install.
