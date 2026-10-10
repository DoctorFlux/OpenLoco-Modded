# UI and UX

## Style

Use the existing OpenLoco window widgets, colours, and string ids. Do not add a separate toolkit. Windows should look like the vehicle and station windows players already know.

Advanced controls stay off the main toolbar. A player who has not installed a mod never sees its window.

## Windows to add

| Window | Opens from | Shows |
| --- | --- | --- |
| Mod manager | Title or options | Installed mods, enable state, version, dependencies, errors, log tail |
| Mod config | Mod manager | Settings declared by the mod schema |
| Shared lines | Vehicle or a lines button | Line name, mode, stops, assigned vehicles, status |
| Timetable | A line | Interval, dwell, targets, lateness |
| Logic editor | A line, yard, or shunter | Blocks for the instruction set, not a second language |
| Logic inspector | The editor | Current instruction, variables, waits, recent events, failure |
| Yard controller | A configured yard | Track roles, programs, job queue |
| Consist | A train | Order of vehicles, couplers, which end is powered |
| Wagon requests | A freight line or yard | Open requests and assigned jobs |
| Path signal | Signal window extension | Path mode versus block mode |
| Reservation debug | Optional overlay | Reserved and occupied pieces, owner, direction |
| Passenger demand | Station | Groups waiting, destinations, connections |
| Cargo demand | Industry and station | Supply, demand, waiting loads |
| World generation | Scenario editor | Size, town spacing, industry density, terrain |
| Object selection | Scenario editor | Search, category, filter, sort, presets, missing objects |

## Behaviour

- Every failed command shows a specific reason: blocked, no path, incompatible wagon, missing dependency, full, timeout.
- Highlight the train, wagon group, or track under discussion. Do not rely on ids alone.
- Destructive actions (discard mod state, delete a yard) name the save file they will write. They never overwrite the original SV5.
- Disable buttons that do not apply to the transport mode. Ships do not get coupler controls.
- Tooltips on logic blocks use the same words as the text form of the program.

## Not in the first UI milestone

The mod manager is the first window. It can list directories the loader already understands. It must not imply that enablement is saved in a game until the save container exists.
