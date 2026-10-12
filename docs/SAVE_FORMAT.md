# Save format

## What the code does now

- Files are SV5/SC5 via Sawyer chunks.
- `kCurrentVersion` in `src/OpenLoco/src/S5/S5.cpp` is `0x62262`.
- `kMagicNumber` in `S5.h` is `0x62300`.
- `importSaveToGameState` throws "Unsupported S5 version" unless the header version matches exactly.
- `S5::GameState` is a packed blob. `static_assert(sizeof(GameState) == 0x4A0644)`.
- `GameStateType2` (`0x49EA24`) is the only older layout conversion, and it is a company-record size change, not a general migration ladder.
- Orders, routings, entities, stations, and objects are fixed arrays inside that blob. See `ENGINE_LIMITS.md`.
- Mod state cannot be added there without breaking every current save.

## Upstream

The January 2026 development post says NSF is planned to raise map and other limits, and that the team wants save fields identified before designing it. There is no NSF reader or writer in this checkout. Do not ship a second long-lived format that upstream would have to fight.

## Strategy

1. Keep the SV5 importer byte-compatible for `0x62262`.
2. When extended state is required, write a new file. Never replace the source SV5.
3. Design that file as a versioned chunk container that can later be renamed or wrapped if upstream NSF lands. The chunk names used by mods stay stable even if the outer envelope changes.
4. Reject a sidecar as the long-term authority. A sidecar can be separated from the save and will desync. A short-lived debug dump is allowed if it is clearly not loaded as truth. Companions exist by user request until the container lands: `<save>.SV5.olines` for shared lines and `<save>.SV5.osignals` for signal types and pending reservations (D20). Block rules write version 1 `<save>.SV5.orules` (D26/D27). The lines and signals companion versions and packed S5 state are unchanged.
5. Old-format export is optional and only legal when every live feature can be represented in SV5. Parked-wagon jobs, block rules, and larger maps cannot.

## Proposed container, not implemented

```
header: magic, container version, game tick, PRNG state
chunk vanilla: imported SV5 body or a normalized snapshot
chunk mods: list of { id, version, stateSchemaVersion, enabled }
chunk <mod id>: bytes owned by that mod, validated against its schema
checksum of the chunk list
```

Rules:

- Unknown future container versions fail with a named error.
- Unknown mod chunks are kept only if the save is opened read-only for inspection. Playing requires every listed mod or an explicit "drop this mod's state" choice that is itself saved as a new file.
- Migrations are functions from schema N to N+1, tested per mod.
- Entity references inside mod chunks use engine persistent ids, not raw pool indexes.
- Checksums detect truncation. They are not a security boundary.

## Tests required before this is used

- Load a vanilla SV5 and save the vanilla subset to a byte-identical result through `GameSaveCompare` where the subset is defined to be identical.
- Import SV5 and write the new file without modifying the source path.
- Load the new file, including a mod chunk, and reject a bad checksum.
- Load a file whose mod is not installed and surface the missing id.
- Migration from schema 1 to 2 for a sample mod.

Until those tests exist, no gameplay feature may write the proposed container. Approved `.olines`, `.osignals`, and `.orules` companions are the temporary exceptions described above.

## Rules companion v1 (implemented, TESTING)

Rules load after lines, validate owner identity and exact order contents, and store keyed rows, blocks and independent resumable vehicle execution. Missing files clear rules; malformed files warn and are rejected. Mismatched owners are skipped with a named warning. Saving writes `.orules.tmp` and replaces the companion only after successful serialization and close. Numbers use the classic locale regardless of the game display locale. A write or replacement failure fails the save operation. Switching saves and new-game order reset clear stale rules. External renames/copies must preserve all matching companion names. This is the approved temporary companion exception, not an NSF/NOF implementation.

Rules v1 retains the old kind-2 wire value for Else; Else if and Break append kinds 7 and 8. Repeat number zero and its active zero counter mean Forever; finite repeats validate positive remaining counts. Break stores the stable key of an enclosing loop. The loader validates branch chains and loop targets before attachment. Older builds cannot read companions containing the appended kinds; the rollback checkpoint and its disposable saves remain separate. Packed Order/S5, `.olines` v5 and `.osignals` v3 are unchanged.

Container drop intent (`Edit.into`) and closing-slot geometry are transient. Dropping orders inside a section updates its stable first/last keys and dependent bounds atomically; the existing v1 rules companion serializes those expanded definitions. No new companion fields or versions are required.

The arrival-timing extension appends `PHASES 1` after the existing v1 records. It stores non-Automatic block timing by owner/block key, followed by each saved vehicle's pending arrival block, physically reached row, already-consumed entry row, approach anchor, reached flag and station-arrival processing flag. Timing, keys, cursor/instruction correspondence and physical-state consistency are validated before any attachment. This is an optional extension to the existing companion, not a packed-save change. Older companions without it remain readable with Automatic timing; older builds reject the extension. A legacy land wait whose planner is ahead of the physical vehicle resumes its approach before restarting its timeout. Keep matching SV5 and companions together and keep checkpoint saves separate (D36/D37).
