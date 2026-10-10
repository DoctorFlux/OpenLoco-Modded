# Programmable logic

One interpreter for lines, yards, shunters, and dispatch. It is not implemented. This spec is the contract.

## Representation

A program is a list of instructions with integer ids. The visual editor and the text view are two displays of that list. They are not two runtimes.

Each instruction has a type, operands, and an optional next-id. Control instructions have branch targets. There is no arbitrary jump to an address.

## Instructions

Control: `If`, `ElseIf`, `Else`, `And`, `Or`, `Not`, `Compare`, `Switch`, `Repeat`, `Loop`, `WaitUntil`, `WaitEvent`, `Timeout`, `Break`, `Call`, `Return`.

Actions: `GoTo`, `Wait`, `Load`, `Unload`, `ChangeLine`, `RequestWagons`, `ReleaseWagons`, `AssignShunter`, `Couple`, `Uncouple`, `SplitConsist`, `Reverse`, `MoveToTrack`, `SortWagons`, `Assemble`, `Dispatch`, `SetPriority`, `ReserveRoute`, `CancelRequest`, `CallSubroutine`.

`Couple`, `Uncouple`, `SplitConsist`, `Reverse`, `MoveToTrack`, and `SortWagons` require the rail capability. The editor hides them for road, sea, and air programs.

## Conditions

Operands may read: cargo waiting, cargo type, cargo quantity, wagon count, wagon type, free capacity, train length, idle locomotives, platform free, siding occupied, reservation state, passenger demand, freight destination, lateness, date and tick, direction, consist composition, distance, shunter availability, open wagon requests, industry supply or demand.

Conditions that need a missing mod evaluate to a defined failure, not to true. Example: passenger demand is false and flagged `capability-missing` when that mod is off.

## Execution

- The host runs at most N instructions per tick. N is a constant, initially 256, then the program waits until the next tick.
- `Loop` and `Repeat` carry a remaining-iteration count stored in the program state. A loop without a bound is rejected at load.
- `WaitUntil` and `WaitEvent` arm a condition and stop the program until the event or a timeout.
- `Timeout` and `Cancel` move the program to a failure block or to idle.
- `Call` depth is limited. Exceeding it is a fault, not a stack overflow.
- Faults record the instruction id and a reason. The vehicle or job stops safely. It does not keep the last command half-applied. Commands are atomic.
- Programs are deterministic: same state and same events produce the same trace.

## Data

Variables are typed slots: bool, int, entity ref, cargo type, line id. No strings executed as code. No unbounded maps. Slot count is capped per program.

Subroutines are programs in the same package with a name. Recursion uses the call-depth limit.

## Save

Program definitions and the wait/pc/variable frame are mod state. They are not stored until the save container exists. A running vanilla order list is unchanged if a program is not attached.

## Debugger

The inspector shows the current instruction, variable slots, armed wait, the last 32 trace events, the fault reason, and queued actions. It is read-only during a multiplayer session if that session ever exists.

## Vanilla orders

A vehicle may use vanilla orders or a program, not both writers. Switching is a command that replaces the active controller at a safe point (stopped, or at a station).

## Tests

Unit-test each control instruction and the step budget before any UI. In-game tests come with the editor milestone.
