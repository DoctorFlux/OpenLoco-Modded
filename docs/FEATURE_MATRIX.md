# Feature matrix

CargoDist is **COMPLETED (user accepted D68, 2026-10-14)** and integrated into master through normal merge `eea93c8d`. Recorded Debug/Release suites pass 566/566 on unchanged source `2d3bb93c`, including 113 CargoDist checks. Historical TESTING/manual gates are superseded for this delivery; individual manual scenarios are not newly claimed as performed. Both release checkpoints remain unchanged; this integration creates no release.

D67 automatic station/vehicle Cargo views omit origin wording and show quantity plus distribution/destination summaries. Station rows reclaim the origin line through shared row geometry. Packet origins/accounting and Classic presentation remain; no new save or language format. Verification is in development status.

D66 extends verification of existing automatic transfers to multiple final destinations in one station/compartment, two-way town bus/tram feeders connected by rail/ship/air, same-town stops and empty freight interchanges with several consumers. Private/shared routes use the same system. Native feeder/final revenue linkage is tested through partial splits, two transfers, reload and delayed zero-payment delivery. Source `fa6d60cc` adds 24 checks to the 542 baseline; current complete-suite results are in development status. D68 accepts CargoDist as COMPLETED. No separate warehouse, large window or new save format is added.

D66 reconfigured Debug/Release game builds succeed and both complete suites pass **566/566**, including 113 CargoDist checks. Source/stamp verification and caveats are in development status; D68 records delivery acceptance.

`Vanilla` means the behaviour already in OpenLoco at `e71abfb3`. `Framework` describes native features and later plans. Most additions are opt-in; D62 explicitly makes CargoDist automatic by default, with Manual available.

| Feature | Vanilla | Framework plan | Default |
| --- | --- | --- | --- |
| Orders | Per vehicle, six types | Shared lines can reference one list | Vanilla orders |
| Conditional orders | No | Implemented, COMPLETED (user accepted): native inline editor in vehicle Route/shared-line Orders, directly editable readable fields, multi-stop/waypoint containers and drag/drop. Rules expands the current window; the line browser stays visible. A list with no blocks runs as it does now | Off until a block is added |
| Timetable | No | Shared-line timetable | Off |
| Block signals | Yes | Kept | On |
| Path signals | No | Path and chain checkboxes on a normal signal, with one-way or two-way placement, and a train-window reservation viewer. Implemented | Off until a player places one |
| Depot coupling | Yes | Kept | On |
| On-track uncouple | No | Shunting mod | Off |
| Run-around | No | Shunting mod, real movement | Off |
| Parked wagons | Not as a job | Persistent entities | n/a |
| Passenger cargo | Anonymous quantity | Native CargoDist groups, transfers and destination views (COMPLETED, D68) | Symmetric automatic; Manual available |
| Freight cargo | Anonymous quantity, origin station stored | Native CargoDist station destinations and transfers (COMPLETED, D68); industry markets/requests later | Asymmetric automatic; Manual available |
| Map size | 384x384 | Candidates 512, 1024, 2048 after save work | 384 |
| Object count | Fixed caps, 859 total | Dynamic after NOF-aligned format | Current caps |
| Saves | SV5/SC5 exact version | Packed saves unchanged; matching lines/signals/rules/CargoDist companions; new container deferred | SV5/SC5 with optional companions |
| Mods | None (`plugin.dat` is unrelated) | Loader removed (D23); gameplay features use native windows | None |
| Scripts | None | No script language. Rules are native blocks in vehicle Route/shared-line Orders; Rules toggles the inline palette | None |
| Multiplayer | Incomplete, UI hidden | Later, command replication | Unchanged |
| Native mod DLLs | None | Rejected by default | n/a |

## Native feature surface

Published as [26.09-modded.2](https://github.com/DoctorFlux/OpenLoco-Modded/releases/tag/v26.09-modded.2) on 2026-10-14, with a verified Windows x64 Release ZIP and SHA-256. Fresh Release suite passed 447/447; master was the only branch at release cleanup; CargoDist development used its own branch; D68 integrates it into master through `eea93c8d`. Historical branch references below identify preserved implementation history, not current active branches.

Block rules are implemented and accepted by the user and integrated into master via `28e64e62` (D59, 2026-10-14). Shared lines, timetables and path signals are also COMPLETED by subsequent user acceptance (D61); historical unitemized checks are retained. Latest UI checkpoint `be510fcb` (D58, following D57 `91037492`) on `codex/native-inline-rules` includes D56 safety checkpoint `cf47b6db`, placement fix `3027c2d8`, signal fix `4f5faaae` and block-rules checkpoint `e42b1da8`. Reconfigured Debug/Release builds and complete suites each passed 447/447; focused Rules passed 182/182 (20 new UI regressions), with earlier 87 focused safety/placement checks preserved. D54 and D55 failed the user's native movement recheck; D56 fixes waypoint protection ending before an occupied or reserved continuation. Shunting, expanded objects and larger worlds below remain later plans. CargoDist destination systems are COMPLETED by D68 user acceptance. See [development status](DEVELOPMENT_STATUS.md#current-delivery-snapshot--2026-10-14) for the consolidated delivery and manual checks.

| Feature | Player-visible surface | Still works without opting in |
| --- | --- | --- |
| Block rules | Native inline C blocks in Vehicle Route/shared-line Orders; Rules expands/restores that host and reveals an independent 88-pixel palette, Actions/Details/bin while retaining the line browser and existing controls. Directly editable medium-font fields work with the palette hidden. Stop/waypoint rows use native bold captions, white circle numbers and the plain list background within shaped logic (D58). Current program/next destination, green/red feedback, white arrow, Else/Else if, Forever/Break, multi-row insertion/resize, validated drag/drop and world-number refresh remain. Wait alone exposes When; saved timing and native braking are preserved (D57) | Orders and lines with no blocks |
| Shared lines | Line window; Path/All map overlays evaluate complete routes from current conditions using the selected vehicle's facts; inactive multi-row clauses disappear and vehicle movement/cursor changes preserve geometry, with incrementally built, atomically published geometry, stable All-mode lanes, retained paths during construction refreshes, geometry reuse for metadata edits and preview-limit feedback | Per-vehicle orders |
| Timetables | Timetable window on a line | Lines without schedules |
| Native rail placement | Shared checked reverse-position conversion in placement, pickup preview and train reversal; unequal curve-direction sample counts cannot overrun the destination span. Invalid placement inputs are rejected before vehicle changes (D51, native acceptance TESTING) | Existing placement tool and occupation checks |
| Path signals | Native path/chain/combined flags and reservation view; directional waypoints require a non-circular route to the target, blocked required-waypoint approaches and waits use the actual native danger frame 0 through independent display owners, including unplanned signalled junction arms and unsignalled planner decisions; pending paths recheck physical occupation before admission, complete conflicting curve/movement footprints recheck at actual physical entry through ordinary-signal handoffs, and pass overrides cannot bypass special-signal reservation checks; junction-arm signals gate early and genuine alternatives remain available; waypoints no longer end protection before occupied/reserved continuations, and advancing them replans later orders (D48/D49/D50/D52/D53/D54/D55/D56, TESTING) | Block signals |
| Shunting | Yard tools and job list | Depot coupling |
| Cargo demand | Compact native Cargo destination summaries, Automatic/Classic and transfers; industry requests later | Manual available; automatic asymmetric default |
| Passenger destinations | Compact native Cargo destination summaries, Automatic/Classic and transfers | Manual available; automatic symmetric default |
| Expanded objects | Richer object picker | Current picker |
| Larger worlds | Generation size options | 384 map |

## CargoDist (COMPLETED, D68)

Automatic passenger/mail/freight distribution works on private and shared orders through native cargo handling. D65 replaces standalone settings/destination/capacity views and cargo map links with compact native Cargo-tab summaries and Options > Miscellaneous > Cargo transfers (Automatic/Classic/Custom saved). Automatic handoff uses normal routes and combined StationIds, guarded reached-stop occurrences and useful onward costs. Destination Rules checks remain. Matching `.ocargodist` v1 preserves packets, allocation cursors, observations, credits and graph jobs. Old/native amounts migrate without packed changes; Manual delegates classic distribution. Latest D67 Debug/Release suites each pass 566/566, including 113 CargoDist checks; D68 records user acceptance. Build caveats and evidence are in development status. No consists/shunting/NSF/NOF milestone is included.

D65 supersedes the large-window workflow. No Transfer/Unload orders are required for feeder/interchange/consumer journeys. Useful through cargo remains aboard; repeated shared-stop restrictions are occurrence-specific and the returning feeder cannot reload its transfer. Shared row geometry/caches serve native drawing, expansion, hover and scrolling; Classic/empty rows stay native. Baseline for this follow-up is 520 full tests. Current verification and user acceptance are recorded in development status; D68 accepts CargoDist as COMPLETED.

Final D65 source `c54cfbc8` is verified in both configurations against the 520 baseline; both executables carry `v26.09-modded.2-9`. D68 supersedes the earlier manual acceptance gate. Evidence and symbol-warning limits are in development status.
