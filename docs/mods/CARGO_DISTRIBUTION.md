# Automatic cargo transfers (D65)

Status: TESTING. Branch: `codex/cargo-distribution`. Preserved checkpoint: `e4d7bbb2` (D64 code `22d67d05`). Starting Debug/Release baseline for this follow-up: 520 tests each. Master and both release tags remain unchanged.

## Approved delivery

One native CargoDist system handles passengers, mail and freight across all vehicle modes, private orders and shared lines. It uses existing production, reachable accepting stations, partial loads, automatic transfers, capacity balancing and stable bounded routing. Cargo aggregates share origin, destination, age and transfer accounting; no individual passenger agents are created.

New games and older saves use symmetric passengers/mail and asymmetric other cargo. Automatic uses symmetric passengers/mail and asymmetric freight. Classic selects Manual for all cargo. Existing mixed saved modes display Custom (saved), and are retained until the player explicitly selects a preset. This supersedes the earlier classic-by-default proposals in CARGO_DEMAND and PASSENGER_DESTINATIONS. Manual uses existing gameplay.

Possible rule branches participate in connectivity without changing actual execution. Waypoints never transfer cargo. Native movement, braking and signal safety remain independent. Virtual feeder credits affect vehicle profit; company cash is paid only at final delivery using Locomotion's economy formula.

## Storage and controls

Packed Order, vehicle/station records, GameState and SV5/SC5 layouts stay unchanged. Existing `.olines` v5, `.osignals` v3 and old `.orules` records remain readable. Version 1 `<save>.SV5.ocargodist` stores cargo settings, packets, identity/quantity validation, observations, accounting and resumable graph jobs. Missing or rejected data reconstructs native quantities with named diagnostics and no invented feeder credit. Writes use temporary-file replacement.

D65 supersedes the D62/D63 large-window workflow: use ordinary orders, shared lines and optional Rules. Native Cargo tabs provide compact expandable summaries, with a Cargo transfers preset in Options > Miscellaneous. Separate settings/destination/capacity windows and cargo congestion overlays are removed; existing Path/All route displays remain. Destination cargo checks extend the existing Rules editor. No shunting, consist editing, industry production changes, NSF/NOF or external runtime is added.

## Verification

Require quantity conservation, direct/multimodal transfer routing, symmetric/asymmetric/manual behaviour, capacity alternatives, possible branches and loops, native timing/losses, partial compartments, exactly-once company cash, migration, lifecycle/reused IDs, save/load during transfer and graph work, stale-state clearing, bounded/deterministic execution and native inline row geometry, expansion and scrolling. Build both configurations and run both complete suites. The user controls the short coal-chain, passenger-transfer and save/reload acceptance check. Keep TESTING until recorded acceptance.

## Reference

Behaviour reference: [OpenTTD CargoDist manual](https://wiki.openttd.org/en/Manual/Passenger%20and%20cargo%20distribution), [demand calculation](https://github.com/OpenTTD/OpenTTD/blob/master/src/linkgraph/demands.cpp), [flow routing](https://github.com/OpenTTD/OpenTTD/blob/master/src/linkgraph/mcf.cpp), [transfer accounting](https://github.com/OpenTTD/OpenTTD/blob/master/src/economy.cpp). Implementation remains native to this MIT repository.

## Implemented surfaces

Create a road stop at the mine, a combined road/rail station, and a station serving the consuming industry. Give the truck normal StopAt orders for the mine/interchange and the train normal StopAt orders for the interchange/consumer. No Transfer or Unload instruction is required. Base-game combined facilities share one StationId and one waiting pool. Separately named nearby stations require a connecting service; proximity does not transfer cargo.

Station and vehicle Cargo tabs retain their native size, quantity captions, sprites, refit/catchment controls and other tabs. A nonempty automatic entry adds one compact destination/next-hop summary. Click its row to expand/collapse destination quantities using the existing scrollbar. Classic and empty entries keep their native height. Drawing, scrolling, hover and clicks use a single cached variable-row layout. Queries are read-only with an 8192-packet budget per summary refresh, at most 64 detail lines per row and a 32000-pixel scroll bound. Expansion is local to the window; closure, owner change or a different game clears it. Finding route and Waiting for connection identify pending/unavailable routing.

Options > Miscellaneous places Cargo transfers beside Lines without growing the window. Automatic and Classic submit the existing validated settings command for the current game. Custom (saved) is a read-only indication of mixed modes. Hidden distance/return/accuracy values survive preset changes and save/load. Dropdown callbacks check the game generation, controlling company and settings snapshot. Destination checks remain available in Rules.

The graph seeds compatible service legs from a read-only abstract interpretation of the real compiled order rules, preserving exclusive Else chains, Skip, Go to, Repeat and Break. Service collection, graph setup and route publication share the 4096-step simulation-tick budget with route edge scans and retain complete publications during replacement. Up to four candidate first hops per destination permit alternative carriers; a slower service remains eligible if its next hop reduces the published remaining destination cost. Queued legacy cargo receives fair deterministic destination allocation in bounded batches, using a saved allocation cursor without changing the game PRNG. No cargo map overlay or Planned view is exposed in the D65 player workflow.

Capture the physically reached StopAt before native advancement, storing its owner, stable key, revision and service signature transiently. Outgoing rides follow the possible stop-key graph from that occurrence, rather than treating every mentioned station as boardable. Repeated visits apply their own shared-line cargo filters. Equivalent logical rides from one vehicle count its physical capacity only once. StopAt is the only cargo boundary; waypoints and route-through rows only guide movement.

A boarding hop must decrease the published remaining journey cost. This keeps useful alternatives while preventing the returning feeder from immediately reloading its transfer. Through cargo stays aboard if that vehicle can usefully continue; otherwise it unloads at the physical interchange. Acceptance at an intermediate station does not deliver packets addressed elsewhere. Explicit unloading, no-load/no-unload restrictions and full-load startup remain supported. Loading never forces a condition or alters Rules execution. When a completed routing plan removes a destination connection, waiting cargo can receive another reachable destination at its next cargo boundary, preserving quantity, age and origin. Unresolved repeated-stop context preserves onboard cargo and refuses speculative boarding.

Cargo transfers occur only through the real native station lifecycle. Both primary and secondary compartments are accounted for. Continue native cargo delays, station ratings/losses, ageing, acceptance and final industry/town delivery. Split feeder credit exactly across partial groups. Cash follows the original final-delivery path; virtual credit adjusts vehicle reporting only. Cargo carrier changes during articulated reversal move the metadata, and removal clears it. Deleted origins retain their coordinates for income and are marked unavailable rather than relabelled as reused stations.

Two Checks join the existing inline Rules editor: Cargo waiting for a destination and Cargo onboard for a destination. They select a cargo and a stable stop key and use existing comparisons. Missing current stations, Manual mode or unfinished routing can produce an unavailable result with a reason. Old `.orules` records still load; builds predating these appended check values reject companions containing them. Keep checkpoint saves separate.

## Companion v1

`OCARGODIST 1` is a bounded classic-locale text payload with its own checksum and a fingerprint of the matching native save. Records contain distribution settings, canonical holder identities and packet totals, allocation cursors, origin coordinates/removal markers, station/service observations, published routes, pending service-collection, graph-setup and routing queue/cursor/results, last visits and unclaimed virtual income adjustments. Matching native quantities and all record bounds/references are checked before atomic attachment. Reached-stop contexts and inline UI caches are not serialized: they rebuild from matching orders, Rules runtime or the safe native cargo cursor after load; ambiguous context preserves cargo. Writes close and validate a temporary companion before replacement. A failed write fails the save; a bad companion is named in the load warning and native amounts are reconstructed. Missing files enable automatic migration. It is a companion exception to the deferred save-container phase, not NSF.

## Limits and acceptance

The service graph describes possible order connectivity and observed journey times; it does not promise a physical connection or predict cargo-dependent branches. Actual motion and reservations decide whether a vehicle can travel. A blocked service or permanently untaken branch can leave cargo waiting. Metadata, service-edge and published route caps are 262144 records, with reserved native-holder space; assignment is limited to accuracy times 16 units per call when several destinations exist. Limited forecasts can show a subset; narrow the cargo/company filter for large networks. These are explicit bounded-work limits rather than a change to engine map/object/entity caps.

Final reconfigured Debug/Release game builds succeeded; complete suites each passed 514/514 with 67 CargoDist checks. The prior focused Debug suite passed 65/65. Build caveats and evidence are recorded in development status. Native visual acceptance is the user's short transfer, destination view, overlay and save/reload check. No automated game control is used.
