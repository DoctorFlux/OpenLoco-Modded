# Path signals and reservations

Not implemented. Block signals stay the default.

## What exists

`Routing.cpp` finds the next signal and treats the section as occupied or clear. Each vehicle holds up to 64 routing words (`kMaxRoutingsPerVehicle`). `TrainSignalObject` has side and lights, not a path-signal flag. `CompanyAiPathfinding.cpp` is for building track, not for train reservations.

## Reservation model

A reservation is a set of track pieces plus direction, owner (vehicle or wagon group), and a release condition (tail passed, job cancelled, timeout).

Two reservations conflict when their pieces overlap in a way the geometry says cannot be occupied together. Parallel tracks do not conflict. A crossover does. A wagon standing on a piece conflicts with every reservation of that piece.

The check uses track connectivity from `Map/Track`, not a single block bit. Block signals remain a coarser mode: they reserve the whole section as they do now.

## Required behaviour

- A move is refused when the path conflicts.
- Push and reverse reserve the path in the travel direction, including the length of the consist.
- Parked wagon groups own their pieces.
- Release happens when the tail clears, or when the command is cancelled.
- Shunting signals are a signal object mode that allows a reserved shunt path without opening the main block.
- Priority can preempt only a reservation that is itself waiting, never one that a train is already on.
- Deadlock detection reports the involved ids and breaks the lowest-priority waiting reservation.

## Integration

Existing signal windows gain a mode. Automatic signal placement is unchanged for block mode. Path mode is opt-in per signal.

Longer than 64 reserved pieces requires the save container, because the routing ring is inside `S5::GameState`.

## Tests

Junction, crossover, parallel tracks, sorting yard, reverse, and two non-conflicting trains in one signal area. Plus a conflict that must not be entered.
