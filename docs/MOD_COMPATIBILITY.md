# Mod compatibility

## Session rules

A set of mods may activate together only when all of the following hold:

- Each `engineApiVersion` is compatible with `kModApiVersion`.
- Each required capability is provided by this build. Capability checks are not enforced yet.
- Hard dependencies are present and their versions satisfy the dependency constraint. Version constraints between mods are not implemented yet; only presence is checked.
- No declared conflict is present.
- The graph has no cycle.
- The platform list includes this host, or is empty.

The resulting order is dependency-first and stable for a given set of ids.

## Saves

A save records the id, version, and `stateSchemaVersion` of every mod that wrote state. Loading into a session:

- Missing mod: warn and refuse to simulate until the player installs it or explicitly discards that mod's chunk into a new file.
- Older schema: run the mod's migration, or refuse.
- Newer schema: refuse. Do not guess.
- Same id, incompatible version: refuse unless that mod documents a migration.

Vanilla SV5 files have an empty mod list and load with classic rules.

## Multiplayer

Not implemented. The intended check, before a session starts, is:

- Same engine build or an explicit compatible API range.
- Same activated mod ids and versions.
- Same capability set.
- No mod that flags itself non-deterministic.

Wall-clock logic and unshared random calls fail this check by policy even if the code happens to run.

## Uninstall and upgrade

- Disable removes the mod from the active set. Its saved chunk remains until the player discards it.
- Upgrade runs migrations in order.
- Uninstall of a hard dependency disables dependents and lists them.
- Original Locomotion objects inside a mod are not allowed, so uninstall never has to restore proprietary files.

## What the loader does not decide

It does not merge two mods that patch the same vanilla order list. Gameplay conflicts that are not declared in `conflicts` are bugs in those mods. The engine should still reject two commands that claim the same exclusive capability once capabilities are enforced. That exclusive set is not defined yet; record it in `DECISIONS.md` before two mods can both own lines or yards.
