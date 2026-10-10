# Feature matrix

`Vanilla` means the behaviour already in OpenLoco at `e71abfb3`. `Framework` means this project will add it. `Default` is what a player gets with no mods enabled.

| Feature | Vanilla | Framework plan | Default with no mods |
| --- | --- | --- | --- |
| Orders | Per vehicle, six types | Shared lines can reference one list | Vanilla orders |
| Conditional orders | No | Programmable logic | Off |
| Timetable | No | Shared-line timetable | Off |
| Block signals | Yes | Kept | On |
| Path signals | No | Optional signal mode | Off |
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
| Scripts | None | One logic runtime, language not chosen | None |
| Multiplayer | Incomplete, UI hidden | Later, command replication | Unchanged |
| Native mod DLLs | None | Rejected by default | n/a |

## Install surface

| Mod | Player-visible when installed | Still works if absent |
| --- | --- | --- |
| Programmable logic | Logic editor, extra order actions | Normal orders |
| Shared lines | Line window | Per-vehicle orders |
| Timetables | Timetable window on a line | Lines without schedules |
| Path signals | Signal type and reservation view | Block signals |
| Shunting | Yard tools and job list | Depot coupling |
| Cargo demand | Destination and request UI | Classic freight |
| Passenger destinations | Demand and transfer UI | Classic passengers |
| Expanded objects | Richer object picker | Current picker |
| Larger worlds | Generation size options | 384 map |
