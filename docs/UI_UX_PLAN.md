# UI and UX

## Style

Use the existing OpenLoco window widgets, colours, and string ids. Do not add a separate toolkit. Windows should look like the vehicle and station windows players already know.

The Rules button sits on the vehicle window and the line window. The Rules window stays closed until the player opens it. It is specified in `docs/mods/PROGRAMMABLE_LOGIC.md`.

## Windows to add

| Window | Opens from | Shows |
| --- | --- | --- |
| Mod manager | Removed | The title-screen Mods button and the mod manager window are gone |
| Line manager | Click the in-game top toolbar Lines button, or Options > Miscellaneous | Normal window colours. Orders tab with numbered orders, slots, and Local/Express. Vehicles tab can add or buy a vehicle. The vehicle route tab keeps the normal order list and has a Line button |
| Gameplay options | Options > Miscellaneous | Existing disable-breakdowns checkbox. Added no obsolescence, infinite money, and a calendar speed dropdown (Normal, Slow, Frozen). These are engine settings in `openloco.yml`, not installable mods |
| Mod config | Mod manager | Settings declared by the mod schema |
| Shared lines | Vehicle or a lines button | Line name, mode, stops, assigned vehicles, status |
| Timetable | A line | Interval, dwell, targets, lateness |
| Rules | The empty tab slot between Finances and Route on a vehicle, or the empty 24 by 24 slot under the order down button on a line | One window. Palette on the left, that order list in the centre, blocks around one row or a group of stops and waypoints. Dragging stays inside the window. Footer shows the current block, the wait, or the last fault. Spec: `docs/mods/PROGRAMMABLE_LOGIC.md` |
| Yard controller | A configured yard | Track roles, the Rules window, job queue |
| Consist | A train | Order of vehicles, couplers, which end is powered |
| Wagon requests | A freight line or yard | Open requests and assigned jobs |
| Path signal | Signal tab checkboxes in rail construction | Path and chain checks on the normal signal, plus the existing one-way and two-way buttons. Arrows: white two-way path, yellow one-way path, red/blue/green chain aspect |
| Reservation viewer | Train window main tab | One pressed button shows that train's reserved path on the map |
| Passenger demand | Station | Groups waiting, destinations, connections |
| Cargo demand | Industry and station | Supply, demand, waiting loads |
| World generation | Scenario editor | Size, town spacing, industry density, terrain |
| Object selection | Scenario editor | Search, category, filter, sort, presets, missing objects |

## Behaviour

- Every failed command shows a specific reason: blocked, no path, incompatible wagon, missing dependency, full, timeout.
- Highlight the train, wagon group, or track under discussion. Do not rely on ids alone.
- Destructive actions (discard mod state, delete a yard) name the save file they will write. They never overwrite the original SV5.
- Disable buttons that do not apply to the transport mode. Ships do not get coupler controls.
- Words on a block are the words in the tooltip.
- The Rules window is placed beside the window that opened it. If that spot would cover the line window, the vehicle window, or schemes, it moves until it no longer covers them.
- The vehicle route column already holds unload, wait, skip, delete, up, down, and reverse. The line orders tab already holds Local, Express, Loop, the timetable slot, and auto spacing. Rules does not take those widgets.

## Not built yet

The Rules window is specified and has no widgets in the game. The mod manager described in older notes is gone.
