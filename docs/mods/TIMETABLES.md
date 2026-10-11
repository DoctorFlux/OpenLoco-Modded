# Timetables

Implemented on the shared line. `decideDeparture` waits for the closest future slot, applies unbunch or auto-unbunch, and sends a late vehicle out immediately. Waiting is never used to make a vehicle faster. The line window can add a slot to the last stop.

## Rows

For each stop: target arrival tick offset, target departure, minimum dwell, maximum dwell. A line also has a target interval between vehicles.

## Behaviour

- Even spacing uses the distance along the line. A vehicle that is picked up, stopped, or set to the red stop flag is not counted. Loading and unloading still count.
- The even gap is the line length divided by the number of running vehicles. A vehicle holds while the distance to the next running vehicle in front is shorter than that gap. A vehicle behind, including one stop behind, does not release it and does not cancel a timetable slot. Two vehicles on the same spot: the lower id leaves and the other holds. The hold grows with the missing distance and stops at one even headway, or ten minutes when no round trip has been measured yet.
- A late vehicle shortens dwell down to the minimum, then departs. It does not skip stops unless the line's recovery rule says so.
- Recovery never teleports.
- A shunt that delays a departure is just lateness. The timetable does not cancel the shunt.
- Replacing a vehicle keeps the slot if the line assignment moves to the new vehicle in the same command.
- Platform limits: if the stop has no free platform, the vehicle waits and the lateness grows. It does not reserve a platform that path signals have not agreed to, once those exist. Before path signals, it uses the current station occupancy.

## Clock

Times are simulation ticks converted to the game date for display. They are not wall-clock times.

## UI

The timetable window is specified in `UI_UX_PLAN.md`. Manual edits and an "even spacing" action are both commands.

## Tests

Early hold, late recovery, and a line that still runs when the shunting mod is absent.
