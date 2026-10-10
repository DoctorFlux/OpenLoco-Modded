# Timetables

Not implemented. Hard dependency: shared lines. Does not require shunting.

## Rows

For each stop: target arrival tick offset, target departure, minimum dwell, maximum dwell. A line also has a target interval between vehicles.

## Behaviour

- Even spacing is computed from the interval and the number of assigned vehicles.
- A vehicle that arrives early waits until its departure target, unless the player disables holding.
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
