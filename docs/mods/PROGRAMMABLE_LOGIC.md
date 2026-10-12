# Block rules

Specified, not implemented. This is the contract for the next milestone after release `v26.09-modded.1`.

The player drags blocks onto an order list, in the style of Microsoft MakeCode. The words on the blocks are short enough for a child to read. There is no text language, no Lua, and no WebAssembly. Decision D26.

A list with no blocks is the current game. A vehicle with its own orders keeps those orders. A shared line with no blocks keeps the line it has today.

## What the first build includes

The first build is the blocks, the Rules window, and those blocks running on both of these lists:

- the orders of one vehicle that is not on a line
- the orders of one shared line, for every vehicle assigned to that line

A block can wrap one row, or a whole stretch of rows. A stretch can hold stations, route-through orders, and waypoints together.

Yard, shunter, cargo-destination, and passenger-destination blocks use this same window later. They stay out of the palette until those features exist. The first build is still the whole order-rules feature for vehicles and shared lines, not a runner with nowhere to put a block.

## The order list

The rows stay the vanilla orders: end, stop at, route through, waypoint, unload all, and wait for a full load. Blocks sit beside that list. They are not new values in the order type.

`Order::_type` keeps its three type bits. The two unused type values stay unused. `sizeof(Order)` stays 1 for the base order. A wider order does not fit the save, and block rules do not ask it to. See `ENGINE_LIMITS.md`.

Each order that rules can see has a key owned by the rules data. The key is not stored in the order byte. Insert, delete, and move update the key map in the same command as the order change. A block stores keys, so adding a stop above a group does not retarget the group.

The end order is the bottom of the list. A block cannot wrap it, and Go to cannot target it.

The existing vehicle button that skips the next order once stays that button. Block Skip is a rule in this window. The two are different controls.

## Blocks

The palette has these blocks:

| Block | On the list | What the player does |
| --- | --- | --- |
| If | A C-shape around one row or a group | Reads one check. The rows inside run when the check passes |
| Otherwise | A second C-shape under that If | The rows inside run when the check does not pass |
| And / Or | One extra sentence on that If | Joins a second check. An If has one extra sentence at most |
| Repeat | A C-shape around one row or a group | Runs that stretch a number of times, then continues after it |
| Wait until | On one stop, waypoint, or group | Holds there until the check passes, or for a number of minutes the player sets |
| Skip | Inside an If or Otherwise | Misses that row, or the whole group, on this pass |
| Go to | Inside an If or Otherwise | The next order is a stop, route-through, or waypoint already on this list |
| Group | A bracket beside a run of rows | Names one stretch so If, Repeat, Skip, and Go to can use it |

The player fills a check from sentences, not from typed code:

- more than
- less than
- the same as

Numbers are small and typed in a box on the block. The rest is a dropdown.

Checks in the first palette, because the game can already answer them:

- Cargo waiting at a named stop on this list is more than, less than, or the same as a number
- This vehicle is full, empty, or has space
- The platform at a named stop on this list is free. This uses the same station occupancy the timetable already uses
- This vehicle is late, or on time, at a stop that has a timetable slot
- The year is before or after a chosen year

A late or on-time check with no timetable slot on that stop takes the Otherwise rows. The Rules footer says the check is unavailable. An unavailable check does not count as passed.

These checks are named now and hidden until their feature exists. They are the same kind of sentence, added to this palette later:

- passengers going to a station
- freight for a destination
- a siding is occupied
- a path is reserved
- a shunter is free

Couple, uncouple, split, and the other yard actions are later blocks in this palette. The rail-only ones stay hidden on a road, sea, or air list.

## A stretch of the route

A group is a contiguous run of orders. It can contain stop-at, route-through, waypoint, unload-all, and wait-for rows. Waypoints count. The player can bracket three stations, or a station, the waypoint after it, and the next station.

Skip on a group misses every row in the bracket, including the waypoints. Go to a row inside a group starts at that row. Repeat on a group runs the whole bracket, then runs it again, up to the number on the block.

The number on Repeat is required. The smallest number is 1 and the largest is 20. A repeat with no number is refused. Reaching the group again later starts the count from the number on the block. Two vehicles on one line each keep their own count.

An order inserted between two members of a group joins the group. An order inserted above or below the group stays outside it. Dragging one row out of the bracket takes it out of the group. A group that would be left empty is removed.

If, Otherwise, and Repeat can wrap a group. They can also wrap a single row. An If can sit inside a Repeat. The limit is four C-shapes around one row. A deeper drop is refused.

## Two examples

List: Town, Mill, waypoint, Port, Depot. No blocks. The vehicle visits Town, Mill, the waypoint, Port, and Depot, then carries on as the line or the vehicle orders already do.

Same list. A group brackets Mill, the waypoint, and Port. The If on that group says cargo waiting at Mill is less than 8, and the inside is Skip. When fewer than 8 loads are waiting, the vehicle goes from Town to Depot and misses Mill, the waypoint, and Port. When 8 or more are waiting, it visits Mill, the waypoint, and Port, then Depot.

Same list. An If says this vehicle is full, and the inside is Go to Depot. Otherwise brackets Mill, the waypoint, and Port. A full vehicle's next order is Depot. A vehicle that is not full does Mill, the waypoint, and Port.

The stops stay on the list either way. Skipping does not delete them.

## Rules window

Rules is its own window. It uses the normal window colours and widgets. It is not a tab of the line window, and it is not a page inside the vehicle window.

The vehicle route tab is already full. Local and Express fill the row above the list, the Line button sits on that same row, and the right-hand column is unload, wait, skip, delete, up, down, and reverse. The empty tab slot between Finances and Route, at the same size as those tabs, is the Rules button. It opens this window. It does not replace the route list.

The line window is 420 by 268. Orders, Vehicles, Local, Express, the order list, the order buttons, Loop, the timetable slot, and auto spacing already occupy their rows. The Rules button is a 24 by 24 button in the right-hand order column, under the down button and above the bottom row. It does not cover the order list, Schemes, the filters, or the timetable controls.

One Rules window is open at a time. Opening Rules for another vehicle or line switches this window. Closing it leaves the vehicle window and the line window open.

The window shows the vehicle name, or the line name when the vehicle is on a line. The left side is the palette. The centre is this order list, with the same numbers the route already uses. The player drags a block from the palette onto one row or onto a selected run. A run is selected by dragging across consecutive rows in this window. The header of a C-shape or a group moves that whole stretch. A block dropped on the bin at the bottom of this window is removed, and the orders stay. An order dropped on the bin is deleted by the existing delete command.

Dragging stays inside this window. A block cannot be dropped on the map, the line list, the vehicle route list, or another window. Adding a stop by clicking the map stays the tool on the vehicle route and the line orders tab.

The plain order lists do not grow new buttons. Their row layout stays as it is. The brackets are drawn in the Rules window.

The window is placed with its left edge against the right edge of the window that opened it, and with the same top. If that rectangle would cover the line window, the vehicle window, or the schemes window, it moves down, then left, until it no longer covers them or it meets the screen edge. It is a separate window, not a panel drawn on top of those lists.

The footer has one line of status for the vehicle currently selected: the block it is on, the wait it is in, or the last fault. There is no second inspector window.

## How a vehicle runs the blocks

When a vehicle is ready for the next order, the runner looks at the blocks on that order and on any group that contains it. The runner then chooses the next order. Movement, loading, unloading, and pathfinding stay the vanilla ones. Go to does not teleport. Skip does not remove the row.

The host runs at most 256 block steps for one vehicle on one tick. A decision that needs more steps waits until the next tick. Wait until arms the check and stops spending steps until the next tick. While a vehicle waits at a station, cargo still loads and unloads, as it does for a timetable hold. A wait on a waypoint holds on that waypoint.

Minutes on a Wait until block are timetable minutes: 40 simulation ticks. They do not read the wall clock, and a frozen calendar does not freeze them.

A fault records the block and a reason. The vehicle keeps the order it already reached. A command is atomic.

The same orders and the same checks produce the same next order. There is no random block.

A vehicle on a line uses the line's blocks. Each vehicle stores its own repeat count and its own wait. Editing Rules while that vehicle is selected edits the line, which matches the way order edits already go to the line.

Assigning a vehicle to a line leaves the line's blocks as they are. Private blocks are not copied onto the line. Taking the vehicle off the line copies the line's orders and the line's blocks onto the vehicle at that moment, the same moment the order copy already happens. From then on, Rules for that vehicle edit the private copy.

A line with vehicles of one kind refuses a check that only makes sense for another kind, in the same way it refuses a stop of another kind.

## Later features

Shunting, cargo demand, and passenger destinations add sentences and actions to this palette. They do not add a second rules window. A yard opens this same window on the yard's list. Until those features exist, their blocks are absent from the palette, and a saved block of that kind takes Otherwise and shows unavailable in the footer.

## Save

Rules are not written into `S5::GameState`. The planned file is `<save>.SV5.orules`, beside the lines and signals companions, version 1. It is not written yet.

A save with no rules file loads with no blocks. Orders and lines still load. Deleting a vehicle drops its private rules in that same command, so a reused vehicle id does not inherit them. Line rules are keyed by line id. Private rules are keyed by the vehicle head id, the same kind of id the lines companion already stores.

A running list with no blocks is unchanged.

## Tests

Before the window: a list with no blocks advances as it does now. Cargo below a number skips a group of two stops and one waypoint. Cargo at that number visits the group. A full vehicle goes to a named stop on the same list. Repeat 2 runs a group twice and then the following order, and the next visit starts at 2 again. Two vehicles on one line share the blocks and keep separate counts. An unavailable late check takes Otherwise. A repeat with no number, a fifth nested C-shape, and a Go to on another list are refused. The 256-step budget pauses until the next tick. The same inputs produce the same next order.

With the window: the Rules button uses the empty vehicle tab slot and the empty line-window slot described above. Opening it does not cover the line window, the vehicle window, or schemes. Dragging a block does not change the map tool. An in-game check is required for the drag and the placement. A unit test does not replace that check.

The companion round-trips a group on a line and a group on a private vehicle. A missing rules file leaves the orders in place.
