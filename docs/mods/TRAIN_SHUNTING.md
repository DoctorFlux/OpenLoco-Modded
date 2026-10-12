# Train shunting

Not implemented. Depends on on-track consists and path reservations. Yard rules also depend on block rules.

## Physical rules

- Wagons move along track. No teleport.
- A detached group stays in the entity list and occupies track.
- Consist order, coupler state, orientation, and travel direction are explicit.
- The first entity is not assumed to be a locomotive. Push and pull are both legal when the reservation allows them.
- Multiple locomotives are allowed when the consist model can represent them. Until it can, the spec does not pretend they work.
- A service or line id stays on the logical train when the physical wagon list changes.
- Run-around is: uncouple, reserve a path to the other end, move, couple. Reverse of the whole consist is the existing reverse command plus reservation checks.

## Jobs

A job is a record: id, type, priority, wagon filter, count, source, destination, assigned shunter, state, dependency ids, timeout tick, last fault.

Types: pickup, delivery, sort, transfer, assemble, split, run-around, distribute.

States: `queued`, `assigned`, `moving`, `coupling`, `uncoupling`, `blocked`, `failed`, `done`, `cancelled`.

Transitions are only those that the state machine lists. `blocked` retries until timeout, then `failed`. `failed` releases reservations and leaves wagons where they are.

Recovery:

| Fault | Behaviour |
| --- | --- |
| Wagon missing | Fail the job, do not grab a different wagon unless the filter allows it |
| Siding occupied | Wait until timeout |
| No shunter | Stay queued |
| Couple failed | Stop, release the new reservation, keep the old consist |
| Train cancelled | Cancel dependent jobs |
| Track removed | Fail and release |
| Consist changed by the player | Revalidate; fail if the filter no longer matches |
| Save/load | Resume the recorded state after the save container exists |
| Deadlock | Detector cancels the lowest priority reservation and reports it |

## Programs

A shunter and a yard use the same Rules window as a vehicle and a shared line. The player drags the same kind of blocks. The first palette does not include yard actions. When shunting exists, Couple, Uncouple, and the other rail-only blocks appear in that palette for a rail list.

Example, as blocks on a list rather than as typed code: If cargo waiting at the coal stop is at least 8, request a shunter, go to the coal wagons, couple 8, go to sorting road A, and uncouple. Otherwise, leave the list alone.

Those yard blocks are hidden until shunting exists. A shunter with no blocks can still run jobs from the scheduler.

## Scheduler

Each tick, consider at most a fixed number of queued jobs, highest priority first, then oldest id. Assignment respects one shunter per job and one job per shunter. Do not scan every industry every tick; jobs are created by events (arrival, request, a waiting block).

## Player controls

Automatic or manual mode per yard. Track roles: arrival, departure, sort, storage, run-around. Wagon filters. The Rules window. Priority. Assembly request from a freight line when cargo demand is installed. Without cargo demand, the player can still create a local request.

## UI

Yard window, job queue, consist window, and reservation highlight. Specified in `UI_UX_PLAN.md`.

## Tests

The scenarios in `TESTING_PLAN.md` for one shunter, two shunters, run-around, push, and save during a move.
