# Shared vehicle lines

Implemented as an engine line list. A vehicle on a line uses that line's orders. A vehicle with no line keeps its own copy. The packed SV5 game state is not resized. Lines are stored in a `<save>.SV5.olines` file written and loaded with the save.

## Model

A line has: id, name, transport mode, ordered stops, priority, and the set of vehicle ids assigned to it. Block rules are optional and live beside those orders. The spec is `docs/mods/PROGRAMMABLE_LOGIC.md`.

Editing the line changes every assigned vehicle. Vehicles do not each own a copy. The current `orders[]` slice per vehicle is the vanilla path and remains for vehicles with no line.

Supported modes: train, bus, tram, truck, ship, aircraft. New line does not ask for a type. A new line starts able to accept every kind, and it stays out of the vehicle filters until a stop or waypoint sets the kind. A waypoint on rail is a train line, on a normal road is road, on tram track is tram, and on water is a ship line. Each stop narrows that set. A bus stop locks the line to road. A stop that is both rail and road stays mixed until a later stop leaves only one kind, so a following road stop makes it a road line. A stop that shares nothing with the set is refused. A normal road station is road, which is bus and truck. A station or waypoint on tram track (`RoadObjectFlags::isRoad` is clear) is tram. Tram still uses the road transport mode, and the kind is what keeps it apart from bus and truck. A vehicle that has been picked up does not affect the kind, and a placed vehicle of another kind stops the line from changing. Buy vehicle and Add vehicle follow the kind that remains: road offers a bus or a truck, tram offers a tram, and a line that is still mixed offers each kind it still allows.

## Behaviour

- Add and remove vehicles without deleting the line.
- Distribute departures only when a timetable is also installed. Without it, vehicles run the shared orders independently.
- A line with no blocks uses the vanilla order rows only. Blocks, when a player adds them, are the Rules window on this same list.
- Statistics: vehicles assigned, trips completed, last arrival tick. Counts update from events, not from a full vehicle scan.

## Window

The line manager is an in-game window with the same colours as the other windows. Open it by clicking Lines on the top toolbar. Hovering the button does not open it. It is also available from Options > Miscellaneous, and it is not on the title screen.

A vehicle on a line uses a copy of that line's orders in the normal order list. The pathfinder, station stops, unload, wait, and reverse are the vanilla ones. The line does not advance the order on its own.

The timetable is a separate 24-hour clock. Forty simulation ticks are one timetable minute. It does not use the game year, month, or day, and a frozen calendar does not freeze it. A selected stop can gain a departure slot. Auto spacing is checked at each stop, after loading and unloading, and holds a vehicle while the distance to the next running vehicle in front is shorter than an even share of the line. A vehicle behind does not release it. The hold grows with the missing distance and stops at one even headway. Cargo still loads and unloads during that wait. A late slot leaves immediately.

The Orders tab numbers stops, route-through orders, and waypoints. Local and Express apply to every vehicle on the line. Click the caption, or click the selected line again, to rename it.

While the line window is open, Path draws the selected line on the map and All draws every line. Without rules, the stroke follows the path a vehicle of that kind would take, including from the last stop back to the first. With rules, Path/All instead shows a bounded live preview of the chosen vehicle's current destinations, including Skip, Go to, branches and loops; see the live preview section below. Stops may repeat, so an order of stop 1, stop 2, stop 3, stop 2 draws those legs and then from the last stop 2 back to stop 1. One-way roads and one-way signals are respected, and the return takes another path when the direct way is closed. A piece the line already travels is drawn once, including the opposite direction on that same road, tram track, or rail. A two-way dead end turns around so the line still reaches the stop. A one-way road or one-way signal does not turn around. On a track or road the stroke uses the same steps a vehicle uses, so a curve, an S-bend, and a slope stay on the piece instead of cutting across it. A straight piece stays straight. When two or more different lines are shown, each whole line is shifted by the same amount so the strokes stay in one piece and sit side by side. A line that runs back over its own track stays on that one path. Selecting a line draws the white numbered circle on each stop and waypoint in the same place and with the same label as a vehicle route. Closing the window clears the drawing. A new line starts in the controlling company's primary and secondary colours. The swatches can change them. Schemes opens the ten vehicle types used by the company colour tab: steam, diesel, electric, multiple units, passenger vehicles, freight vehicles, buses, trucks, aircraft, and ships. Each has a checkbox and, when it is on, its own primary and secondary colours. A car whose type has that checkbox on uses those colours. Every other car uses the line's main colours. Paint recolours assigned vehicles only while that switch is on. On the Vehicles tab, Own keeps that vehicle's current colours, including anything set with the vehicle paint tool, and Line puts it back on the line colours. Using the paint tool on a vehicle that is on a line marks it Own, so a later line repaint leaves it alone. Turning Paint off restores company colours, including a company special scheme, for every vehicle that is not Own.

Above the line list are All, train, bus, tram, aircraft, and ship, using the same vehicle-list tab pictures. All shows every line. Each other button shows lines of that kind. A line with no stops yet is only under All. A line that is still mixed shows under every kind it still allows. The bus button is bus and truck. The tram button is tram. The ship button is water. A line with one kind draws that icon in front of its name. Buy vehicle and Add vehicle stay disabled until a stop has set the kind.

The Vehicles tab shows a horizontal diagram of the numbered stops and where each vehicle is, using the game's number markers, then combined monthly profit and running cost. A vehicle that is loading or unloading sits on that stop. Otherwise the marker is the share of the gap already covered: 20 tiles into a 100-tile gap is 20 percent, using the train itself rather than the routing head. A vehicle that has been picked up is not drawn on the bar until it is placed again. The stop bar is always `1---2---3---4---5---` when there is more than one stop. The last gap is the same width as the others and is 0 to 100 percent of the trip from the last stop back to the first. The train is shown on the first stop only when that gap is finished. The row says which two stops it is between and the percent along that leg. Add vehicle assigns one the player already owns. Buy vehicle opens the build window and assigns the new vehicle to the line.

The vehicle route list is the normal order list. Rules opens a separate window and does not add widgets to this list. A Line button sits on the Local/Express row and chooses Private orders or a line. The list only contains lines that vehicle can use. While that route tab's tool is active, clicking a line in the line list assigns it to that vehicle, the same way clicking another vehicle's order list copies orders. A line of another transport mode is not assigned. While a vehicle is on a line, order changes in that vehicle window are written to the line.

## Vanilla

A vehicle with no line keeps its own orders. Removing a line detaches vehicles back to a copied order list at that moment so they are not left with a dangling id. That copy is a command with a test.

## Save

`Lines::saveCompanion` and `Lines::loadCompanion` run from the path-based save and load functions in `src/OpenLoco/src/S5/S5.cpp`. A save with no companion loads with no lines. The companion is version 5. Versions 1 to 4 still load. Version 5 stores the ten scheme checkboxes and colours, and the vehicles that keep their own paint.

## Tests

Two vehicles observe one inserted stop. A road vehicle cannot be assigned to a rail line. Unassigned vehicles keep their orders. One stop kind locks the line, a rail and road stop followed by a road stop becomes road, and a stop outside that set is rejected.

## Live block-rule map preview

Path and All use block rules when present. The selected line follows the progress vehicle identified by the line window, preferring the related route vehicle; other lines use a placed assigned vehicle, otherwise an assigned vehicle. They show one current route per line because vehicles can choose different branches. A line with no vehicle uses current station/year facts and treats vehicle-dependent checks as unavailable. Numbered circles retain the complete order list and stable labels, including stops omitted by the current route choice.

The preview copies that vehicle's branch choices, repeats, pending decisions and wait state. Current sections remain latched as in the simulation; later entries read current cargo, vehicle fullness/space, platform occupation, timetable and year. Skip and Go to connect the actual chosen endpoints, and Repeat/Break affect progression. A false Wait ends the preview at its hold point; passing or expiring waits reveal the following legs. No cargo transfer, future occupancy or future arrival time is predicted, and drawing never changes orders or runtime. The first leg can start at the physical vehicle's tile. Existing land piece searches, water navigation and air segments draw the selected legs; this preview does not force a platform or reserve a route.

The selected line refreshes every eight UI updates; All shares a round-robin refresh budget. Each projection is limited to 256 block steps and 64 destinations. Only one uncached leg is searched per UI update (4096 nodes for land/water searches), so new routes appear incrementally. An LRU cache holds at most 128 legs and 131072 combined point/piece entries; endpoint changes, structural edits and real construction invalidate the appropriate plans/geometry. Unchanged rendering does no rule/path work. The line footer reports building, wait boundaries, unavailable facts or preview limits. All-lines mode can show a partial preview when its requested routes exceed cache capacity. Closing the window clears the view/cache. Lines without blocks keep their original static paths. Native appearance and performance await user manual acceptance (D44).
