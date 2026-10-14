# Cargo distribution (D62)

Status: TESTING. Branch: `codex/cargo-distribution`. Source checkpoint: local master `d8d13307`. Starting Debug/Release baseline: 447 tests each.

## Approved delivery

One native CargoDist system handles passengers, mail and freight across all vehicle modes, private orders and shared lines. It uses existing production, reachable accepting stations, partial loads, automatic transfers, capacity balancing and stable bounded routing. Cargo aggregates share origin, destination, age and transfer accounting; no individual passenger agents are created.

New games and older saves use symmetric passengers/mail and asymmetric other cargo. Each cargo offers Manual, Symmetric and Asymmetric modes. Saved explicit choices persist. This supersedes the earlier classic-by-default proposals in CARGO_DEMAND and PASSENGER_DESTINATIONS. Manual uses existing gameplay.

Possible rule branches participate in connectivity without changing actual execution. Waypoints never transfer cargo. Native movement, braking and signal safety remain independent. Virtual feeder credits affect vehicle profit; company cash is paid only at final delivery using Locomotion's economy formula.

## Storage and controls

Packed Order, vehicle/station records, GameState and SV5/SC5 layouts stay unchanged. Existing `.olines` v5, `.osignals` v3 and old `.orules` records remain readable. Version 1 `<save>.SV5.ocargodist` stores cargo settings, packets, identity/quantity validation, observations, accounting and resumable graph jobs. Missing or rejected data reconstructs native quantities with named diagnostics and no invented feeder credit. Writes use temporary-file replacement.

Native settings, station grouped Waiting/Planned views, vehicle destinations/credits, shared-line capacity feedback and filtered congestion overlays are part of this delivery. Destination cargo checks extend the existing Rules editor. No shunting, consist editing, industry production changes, NSF/NOF or external runtime is added.

## Verification

Require quantity conservation, direct/multimodal transfer routing, symmetric/asymmetric/manual behaviour, capacity alternatives, possible branches and loops, native timing/losses, partial compartments, exactly-once company cash, migration, lifecycle/reused IDs, save/load during transfer and graph work, stale-state clearing, bounded/deterministic execution and native UI geometry/overlays. Build both configurations and run both complete suites. The user controls the short native transfer/view/overlay/save acceptance check. Keep TESTING until recorded acceptance.

## Reference

Behaviour reference: [OpenTTD CargoDist manual](https://wiki.openttd.org/en/Manual/Passenger%20and%20cargo%20distribution), [demand calculation](https://github.com/OpenTTD/OpenTTD/blob/master/src/linkgraph/demands.cpp), [flow routing](https://github.com/OpenTTD/OpenTTD/blob/master/src/linkgraph/mcf.cpp), [transfer accounting](https://github.com/OpenTTD/OpenTTD/blob/master/src/economy.cpp). Implementation remains native to this MIT repository.

## Implemented surfaces

Open Map > Cargo for settings and congestion links. Station Cargo and Vehicle Cargo have a Destinations button; the line browser has a Cargo button. The native distribution window defaults to 620x340, supports 440x240 through a screen-constrained 1000x700, and has independent selection, grouping, sorting, dropdown and dialog state per window. Station Waiting/Planned groups can be expanded by origin, next hop and destination in six orders. Vehicle rows show actual onboard destinations and virtual credits; line Capacity rows show assigned services. Settings select a cargo or all cargo, Manual/Symmetric/Asymmetric, automatic preset, distance effect, return flow and assignment accuracy. Cargo menus page within the native 32-bit dropdown mask.

The graph seeds compatible service legs from a read-only abstract interpretation of the real compiled order rules, preserving exclusive Else chains, Skip, Go to, Repeat and Break. Service collection, graph setup and route publication share the 4096-step simulation-tick budget with route edge scans and retain complete publications during replacement. Up to four candidate first hops per destination permit alternative carriers; a slower valid service is eligible even when another service is faster. Queued legacy cargo receives fair deterministic destination allocation in bounded batches, using a saved allocation cursor without changing the game PRNG. Planned views estimate queued demand without changing cargo; congestion links include that backlog and use cached read-only drawing.

Cargo transfers occur only through the real native station lifecycle. Both primary and secondary compartments are accounted for. Continue native cargo delays, station ratings/losses, ageing, acceptance and final industry/town delivery. Split feeder credit exactly across partial groups. Cash follows the original final-delivery path; virtual credit adjusts vehicle reporting only. Cargo carrier changes during articulated reversal move the metadata, and removal clears it. Deleted origins retain their coordinates for income and are marked unavailable rather than relabelled as reused stations.

Two Checks join the existing inline Rules editor: Cargo waiting for a destination and Cargo onboard for a destination. They select a cargo and a stable stop key and use existing comparisons. Missing current stations, Manual mode or unfinished routing can produce an unavailable result with a reason. Old `.orules` records still load; builds predating these appended check values reject companions containing them. Keep checkpoint saves separate.

## Companion v1

`OCARGODIST 1` is a bounded classic-locale text payload with its own checksum and a fingerprint of the matching native save. Records contain distribution settings, canonical holder identities and packet totals, allocation cursors, origin coordinates/removal markers, station/service observations, published routes, pending service-collection, graph-setup and routing queue/cursor/results, last visits and unclaimed virtual income adjustments. Matching native quantities and all record bounds/references are checked before atomic attachment. Writes close and validate a temporary companion before replacement. A failed write fails the save; a bad companion is named in the load warning and native amounts are reconstructed. Missing files enable automatic migration. It is a companion exception to the deferred save-container phase, not NSF.

## Limits and acceptance

The service graph describes possible order connectivity and observed journey times; it does not promise a physical connection or predict cargo-dependent branches. Actual motion and reservations decide whether a vehicle can travel. A blocked service or permanently untaken branch can leave cargo waiting. Metadata, service-edge and published route caps are 262144 records, with reserved native-holder space; assignment is limited to accuracy times 16 units per call when several destinations exist. Limited forecasts can show a subset; narrow the cargo/company filter for large networks. These are explicit bounded-work limits rather than a change to engine map/object/entity caps.

Latest focused Debug verification passed 65/65. Final reconfigured Debug/Release builds and complete suites are being recorded in development status. Native visual acceptance is the user's short transfer, destination view, overlay and save/reload check. No automated game control is used.
