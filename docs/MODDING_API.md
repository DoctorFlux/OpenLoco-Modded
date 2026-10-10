# Modding API

Version: `0.1.0` (`OpenLoco::Modding::kModApiVersion`).

This version covers manifest parsing, directory discovery, dependency resolution, and activation of mods named in `enabled.json`. An enabled mod may contain `scripts/activate.json` with only `{"log":["message"]}`. There is no event bus, simulation command bridge, UI registration, or save hook. Activation does not change a playing game.

## Package

Proposed file extension: `.olmod`.

Proposed layout, unpacked or inside a zip:

```
manifest.json
README.md
config/
scripts/
assets/
data/
```

`manifest.json` is required. Other folders are optional. This milestone reads a directory. It does not open zips.

No upstream mod-package standard was found. If OpenLoco publishes one, this layout should be revised rather than kept in parallel. See `UPSTREAM_TRACKING.md`.

## Manifest fields

| Field | Required | Rule |
| --- | --- | --- |
| `id` | yes | Dotted lowercase segments, each starting with a letter. Example: `openloco.sample.hello`. |
| `name` | yes | Non-empty string. |
| `author` | no | String, default empty. |
| `version` | yes | `MAJOR.MINOR.PATCH`, no leading zeroes, no pre-release tag yet. |
| `engineApiVersion` | yes | Same form. Compared with `kModApiVersion` at load. |
| `dependencies` | no | Mod ids that must be present. |
| `optionalDependencies` | no | Ordered first when present, ignored when absent. |
| `conflicts` | no | Activation fails if a listed id is also present. |
| `capabilities` | no | Engine capability names. Stored, not checked yet. |
| `platforms` | no | `windows`, `linux`, `macos`. Empty means all. Other characters are rejected. |
| `description` | no | String. |
| `license` | no | SPDX-style short name recommended. |
| `entryPoints` | no | Script identifiers. Empty means a data-only mod. No DLL names. |
| `stateSchemaVersion` | yes | Integer >= 1. This versions the mod's own saved state, not the manifest grammar. |

Unknown keys are ignored so a newer manifest can still be read for its known fields. Nested objects, comments, and trailing commas are rejected. Strings use JSON escapes. `\u` surrogate pairs are rejected.

API rule: major and minor must equal the running API, and the running patch must be greater than or equal to the required patch. `0.1.x` does not satisfy a mod that asks for `0.2.0` or `0.1.1` when the build is `0.1.0`.

## Loader results

`loadModDirectory` returns discovered mods, a load order, and errors.

- A directory containing `manifest.json` is one mod.
- Otherwise each immediate subdirectory is one mod.
- Symbolic links are skipped.
- Manifests larger than 1 MiB are rejected.
- Any parse, platform, API, dependency, conflict, or cycle error clears `loadOrder`.

`resolveDependencies` does the graph check for manifests already in memory. It does not read files.

## Future API, not implemented

These names are the intended shape. Do not add call sites that pretend they exist.

- `ModRegistry::activate` after capability checks.
- `CommandBus::submit` for every state change.
- `EventBus::subscribe` for tick, arrival, couple, uncouple, signal, and job events.
- `ModSettings` from `config/` with a schema.
- `ModState` blob keyed by mod id and `stateSchemaVersion`, stored in the save container.
- Logging through the engine logger, with a ring buffer the mod manager can show.

Scripts, once a language exists, can call only these services. They cannot open files, spawn processes, or use the network. The host loads data files itself and passes parsed values in.

## Sample

`mods/examples/hello-world` is a valid data-only manifest. `mods/templates/basic` is the copy template. Neither is installed into the game.
