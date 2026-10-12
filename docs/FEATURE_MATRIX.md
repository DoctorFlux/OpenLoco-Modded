# Feature matrix

`Vanilla` means the behaviour already in OpenLoco at `e71abfb3`. `Framework` means this project will add it. `Default` is what a player gets with no mods enabled.

| Feature | Vanilla | Framework plan | Default with no mods |
| --- | --- | --- | --- |
| Orders | Per vehicle, six types | Shared lines can reference one list | Vanilla orders |
| Conditional orders | No | Block rules on a vehicle order list or a shared-line order list. A list with no blocks runs as it does now. A group can hold several stops and waypoints | Off until a block is added |
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
| Mods | None (`plugin.dat` is unrelated) | Directory loader and `enabled.json` log actions. `.olmod` zips are not implemented | None, unless `%APPDATA%\OpenLoco\mods` exists |
| Scripts | None | No script language. Rules are blocks in the Rules window | None |
| Multiplayer | Incomplete, UI hidden | Later, command replication | Unchanged |
| Native mod DLLs | None | Rejected by default | n/a |

## Install surface

| Mod | Player-visible when installed | Still works if absent |
| --- | --- | --- |
| Block rules | Rules window from a vehicle or a line | Orders and lines with no blocks |
| Shared lines | Line window | Per-vehicle orders |
| Timetables | Timetable window on a line | Lines without schedules |
| Path signals | Signal tab type dropdown and coloured arrow markers. No reservation view yet | Block signals |
| Shunting | Yard tools and job list | Depot coupling |
| Cargo demand | Destination and request UI | Classic freight |
| Passenger destinations | Demand and transfer UI | Classic passengers |
| Expanded objects | Richer object picker | Current picker |
| Larger worlds | Generation size options | 384 map |
