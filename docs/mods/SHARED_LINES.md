# Shared vehicle lines

Not implemented.

## Model

A line has: id, name, transport mode, ordered stops, order program or vanilla order template, priority, and the set of vehicle ids assigned to it.

Editing the line changes every assigned vehicle. Vehicles do not each own a copy. The current `orders[]` slice per vehicle is the vanilla path and remains for vehicles with no line.

Supported modes: train, bus, tram, truck, ship, aircraft. The line rejects an assignment whose vehicle mode does not match. Rail-only commands are absent on non-rail lines.

## Behaviour

- Add and remove vehicles without deleting the line.
- Distribute departures only when a timetable is also installed. Without it, vehicles run the shared orders independently.
- Conditional orders and dispatch come from programmable logic when that mod is installed. Without it, the line uses the vanilla order template only.
- Statistics: vehicles assigned, trips completed, last arrival tick. Counts update from events, not from a full vehicle scan.

## Vanilla

A vehicle with no line keeps its own orders. Removing a line detaches vehicles back to a copied order list at that moment so they are not left with a dangling id. That copy is a command with a test.

## Save

Line records wait for the save container. In-memory tests can run earlier.

## Tests

Two vehicles observe one inserted stop. A road vehicle cannot be assigned to a rail line. Unassigned vehicles keep their orders.
