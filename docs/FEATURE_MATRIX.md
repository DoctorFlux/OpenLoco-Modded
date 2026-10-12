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

| Feature | Player-visible surface | Still works without opting in |
| --- | --- | --- |
| Block rules | Actual nested blocks in Rules and normal order lists; current program plus next destination, green/red decisions, Else/Else if, Forever/Break; explicit inside slots grow sections; nested palette blocks and grips; MakeCode-style shapes; dragging refreshes map numbers | Orders and lines with no blocks |
| Shared lines | Line window | Per-vehicle orders |
| Timetables | Timetable window on a line | Lines without schedules |
| Path signals | Native signal flags and train reservation view | Block signals |
| Shunting | Yard tools and job list | Depot coupling |
| Cargo demand | Destination and request UI | Classic freight |
| Passenger destinations | Demand and transfer UI | Classic passengers |
| Expanded objects | Richer object picker | Current picker |
| Larger worlds | Generation size options | 384 map |
