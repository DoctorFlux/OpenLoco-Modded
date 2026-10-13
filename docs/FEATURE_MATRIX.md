# Feature matrix

`Vanilla` means the behaviour already in OpenLoco at `e71abfb3`. `Framework` describes native opt-in features and later plans. `Default` is what a player gets without enabling those features.

| Feature | Vanilla | Framework plan | Default without opt-in features |
| --- | --- | --- | --- |
| Orders | Per vehicle, six types | Shared lines can reference one list | Vanilla orders |
| Conditional orders | No | Implemented, TESTING: block rules on a vehicle order list or a shared-line order list. A list with no blocks runs as it does now. A group can hold several stops and waypoints | Off until a block is added |
| Timetable | No | Shared-line timetable | Off |
| Block signals | Yes | Kept | On |
| Path signals | No | Path and chain checkboxes on a normal signal, with one-way or two-way placement, and a train-window reservation viewer. Implemented | Off until a player places one |
| Depot coupling | Yes | Kept | On |
| On-track uncouple | No | Shunting mod | Off |
| Run-around | No | Shunting mod, real movement | Off |
| Parked wagons | Not as a job | Persistent entities | n/a |
| Passenger cargo | Anonymous quantity | Optional origin-destination groups | Classic |
| Freight cargo | Anonymous quantity, origin station stored | Optional destinations and requests | Classic |
| Map size | 384x384 | Candidates 512, 1024, 2048 after save work | 384 |
| Object count | Fixed caps, 859 total | Dynamic after NOF-aligned format | Current caps |
| Saves | SV5/SC5 exact version | Import SV5, write a new container | SV5 |
| Mods | None (`plugin.dat` is unrelated) | Loader removed (D23); gameplay features use native windows | None |
| Scripts | None | No script language. Rules are blocks in the Rules window | None |
| Multiplayer | Incomplete, UI hidden | Later, command replication | Unchanged |
| Native mod DLLs | None | Rejected by default | n/a |

## Native feature surface

Block rules, shared lines, timetables and path signals are implemented and remain TESTING for the recorded native acceptance items. Latest maintenance checkpoint `023ec618` on `codex/waypoint-signal-aspects` includes placement fix `3027c2d8` and signal fix `4f5faaae` and block-rules checkpoint `e42b1da8`; full Debug/Release suites each passed 413/413, with 73 focused safety/placement checks passed. Shunting, destination systems, expanded objects and larger worlds below describe later planned surfaces, not delivered features. See [development status](DEVELOPMENT_STATUS.md#current-delivery-snapshot--2026-10-13) for the consolidated delivery and manual checks.

| Feature | Player-visible surface | Still works without opting in |
| --- | --- | --- |
| Block rules | Actual nested blocks in Rules and normal order lists; current program plus next destination, green/red decisions, Else/Else if, Forever/Break; explicit inside slots grow sections; nested palette blocks and grips; MakeCode-style shapes with clickable fields, compact layout, optional Details and paged selectors; palette clicks and branch Actions; manual Skip retains the white arrow in Vehicle/Rules; dragging refreshes map numbers; Wait alone exposes When; ordinary blocks use automatic timing, saved explicit timing stays readable, and waits finish native braking before arming | Orders and lines with no blocks |
| Shared lines | Line window; Path/All map overlays evaluate complete routes from current conditions using the selected vehicle's facts; inactive multi-row clauses disappear and vehicle movement/cursor changes preserve geometry, with incrementally built, atomically published geometry, stable All-mode lanes, retained paths during construction refreshes, geometry reuse for metadata edits and preview-limit feedback | Per-vehicle orders |
| Timetables | Timetable window on a line | Lines without schedules |
| Native rail placement | Shared checked reverse-position conversion in placement, pickup preview and train reversal; unequal curve-direction sample counts cannot overrun the destination span. Invalid placement inputs are rejected before vehicle changes (D51, native acceptance TESTING) | Existing placement tool and occupation checks |
| Path signals | Native path/chain/combined flags and reservation view; directional waypoints require a non-circular route to the target, blocked required-waypoint approaches and waits use the actual native danger frame 0 through independent display owners, including unplanned signalled junction arms and unsignalled planner decisions; pending paths recheck physical occupation before admission, protected committed pieces recheck at actual physical entry, and pass overrides cannot bypass special-signal reservation checks; junction-arm signals gate early and genuine alternatives remain available (D48/D49/D50/D52/D53/D54, TESTING) | Block signals |
| Shunting | Yard tools and job list | Depot coupling |
| Cargo demand | Destination and request UI | Classic freight |
| Passenger destinations | Demand and transfer UI | Classic passengers |
| Expanded objects | Richer object picker | Current picker |
| Larger worlds | Generation size options | 384 map |
