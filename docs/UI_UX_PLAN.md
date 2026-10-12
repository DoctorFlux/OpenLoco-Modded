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
- The Rules window is placed beside the window that opened it. If that spot would cover the line window, the vehicle window, or schemes, it tries below and left, then compact sizing. If they still cannot fit, normal movable overlap is permitted (D29).
- The vehicle route column already holds unload, wait, skip, delete, up, down, and reverse. The line orders tab already holds Local, Express, Loop, the timetable slot, and auto spacing. Rules does not take those widgets.

## Current build

Rules has native widgets and is in TESTING. The mod manager described in older notes is gone.

## Block-rules implementation (2026-10-12, TESTING)

The native Rules subsystem is implemented on `codex/block-logic-programming`. See `mods/PROGRAMMABLE_LOGIC.md` for current controls, limits, runtime and companion behavior; see `DEVELOPMENT_STATUS.md` for actual verification and remaining manual checks. Geometry supports 600x360 and compact 360x240, with a pinned palette, scrolling list, section grips, bracket dragging, deletion bin, and mouse-following dragged-item label. Commands validate before simulation mutation. No map tool is claimed by Rules dragging.

Cargo checks offer simple has/no-cargo presets, a Stop selector, a Cargo selector and quantity comparisons. Vehicle boolean checks disable numeric controls. Wait has separate Value and Minutes; Repeat uses Times. Block sentences wrap; controls fit every supported size. Valid drops use the model validator before showing a position. Escape and outside drops cancel, and the footer reports faults or current execution. The manual matrix remains in `DEVELOPMENT_STATUS.md`.

Rule sections now resize with First row/Last row and the selected bracket's end grip. The vehicle Route and line Orders tabs show a clipped second rule line only when blocks exist. Clicking it opens that section in Rules. Stops/waypoints drag directly in those lists as well as Rules; previews, valid insertion positions and outside/Escape cancellation use validated model edits. Recorded true/false conditions render green/red; active markers show program progress. A line's footer identifies the vehicle whose independent execution is shown. The clock occupies free space next to Schemes rather than drawing over its button. These latest controls await user-run in-game checks.
