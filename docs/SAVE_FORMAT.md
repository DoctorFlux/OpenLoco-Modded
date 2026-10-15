# Save format

D69 release update (2026-10-15): **26.09-modded.3 is the first consolidated modded edition (edition 1)**, including accepted CargoDist on master plus shared lines, timetables, path/chain signals and the first visual-logic delivery. It supersedes modded.1/modded.2; the older GitHub releases and local/origin tags were removed after the new package and downloaded assets were verified. Commit history and the accepted CargoDist branch remain. Release notes: [26.09-modded.3](releases/v26.09-modded.3.md). Fresh tagged Debug/Release suites each pass 566/566; package, downloaded-asset and retirement evidence is recorded in development status. This supersedes earlier current-release/checkpoint-preservation statements; their historical tests remain valid. Save-container work, consists and shunting remain deferred.

CargoDist and its existing `.ocargodist` v1 companion are accepted and integrated into master through `eea93c8d` (D68). Packed SV5/SC5, `.olines` v5, `.osignals` v3 and `.orules` v1/PHASES remain unchanged. Keep matching saves/companions together and retain checkpoint saves separately. The new save container and NSF/NOF work remain deferred; this integration adds no persistence fields or versions.

D66 adds no persistence fields or version changes. Multiple destinations/origins, packet age and cumulative feeder credits already use `.ocargodist` v1. Complete multileg tests round-trip those records at both interchange handoffs and before split final delivery, rebuilding transient service/reached-stop context. Packed SV5/SC5, `.olines` v5, `.osignals` v3 and existing `.orules` remain unchanged; no warehouse designation is persisted. Current verification is in development status.

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

Clickable block fields, menu pages, text-dialog sessions, focus and Details layout are transient UI state. They serialize no new records and retain rules v1/PHASES 1, lines v5, signals v3 and packed Order/S5 layouts. Full group names use the already validated 80-character definition limit. Accepting an unchanged validated edit preserves current counters, branch choices and wait start ticks; changed edits use the existing safe-boundary invalidation. The shared-inline editing regression round-trips Forever and existing rules definitions without a format change.

Manual Skip's selected stable row uses the existing saved `Runtime.current` field; no new record is added. The regression saves/reloads immediately after a real Skip command before further simulation. Live line route projections operate on runtime copies, and endpoint geometry caches, polling counters, preview limits and footer messages are transient. Rules v1/PHASES 1, lines v5, signals v3 and packed Order/S5 remain unchanged. Clearing rules or switching views invalidates stale live map layers.

The stable Path/All fix adds transient published geometry, build pins, admission/limit state and fixed lane membership. None is saved. Loading previews read the existing ready/current state on a runtime copy and do not change simulation or saved state. Packed Order/S5, `.orules` v1/PHASES 1, `.olines` v5 and `.osignals` v3 remain unchanged (D45).

D46's current-condition line diagram starts with fresh transient interpreter state. It does not import saved branches/waits/counters into the displayed route; the existing vehicle-progress projection still reads a copy when requested. No persisted model/runtime fields or companion versions change, and the `v26.09-modded.1` checkpoint remains untouched.

The D47 geometry-pending/view-dirty flags are transient and are not serialized. Clearing Rules or Lines immediately clears the view/cache/publications, including before loading a different save; topology refreshes within the current game retain completed snapshots only until replacement. Existing `.orules`, `.olines`, `.osignals` and packed Order/S5 formats remain unchanged.

D48's required-waypoint search policy, waiting-owner sets and red display override are transient. Current native order bytes supply the waypoint identity after loading. Packed Order/S5, `.orules` v1/PHASES 1, `.olines` v5 and `.osignals` v3 remain unchanged. No new signal or order save record is introduced.

D49's edge incoming direction, search ancestry and junction-start decision are transient. No fields are added to Order, S5 or signal companions; existing versions remain. This fix changes how new reservations are selected and does not create a new save format.

D57 editor sessions, selections, callbacks, normal-window sizes and captured scroll state are transient UI only. Expanding Ui::Window to three scroll areas and adding an opt-in WindowEventList flag changes no Order, S5::GameState or companion schema. Existing .orules v1/PHASES, .olines v5 and .osignals v3 load and run with the inline editor; no save conversion or migration is required.

## CargoDist companion v1 (COMPLETED, D68)

The user deferred the save-container phase and approved `<save>.SV5.ocargodist`. It is a temporary subsystem companion; S5/SC5, Order, vehicle/station sizes, `.olines` v5 and `.osignals` v3 are unchanged. Cargo settings, identity-bound packets/quantities, origin history, allocation cursors, station/service observations, published routes, pending graph work, last visits and pending virtual income are persisted under `OCARGODIST 1`. The bounded text payload has a checksum and matching-save fingerprint. Malformed/mismatched data is rejected atomically with a named warning and native amounts reconstruct automatically. Writes use validated temporary replacement; failure fails the save.

Old `.orules` files remain readable. Destination waiting/onboard checks append check values 8/9 to existing v1 definitions; older builds reject companions containing those checks. Keep both release-checkpoint saves separate. Copies/renames of new saves must preserve matching `.olines`, `.osignals`, `.orules` and `.ocargodist` names. Missing cargo companions enable automatic migration with no invented prior feeder credit. Full schema and limits are in `mods/CARGO_DISTRIBUTION.md`.

D65 automatic-transfer follow-up supersedes standalone CargoDist windows and congestion overlays. Use normal StopAt/private/shared/optional-rule routes and base-game combined StationIds. Inline Cargo-tab summaries and the per-game Options Misc Automatic/Classic preset replace the large UI; mixed modes and hidden settings persist. Reached-stop ownership/key/revision/signature context and UI caches are transient and reconstruct after load, preserving cargo when unresolved. Packed saves and companion versions remain unchanged. CargoDist is COMPLETED by D68 user acceptance; no consist/shunting or new save-container dependency is introduced.
