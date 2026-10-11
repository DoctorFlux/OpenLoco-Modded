# Dependencies

Hard dependencies are required to activate. Optional integrations add behaviour when the other mod is present and must not be required for the base feature.

```
Engine foundation
    |
    +-- Programmable logic
            |
            +-- Shared lines
            |       +-- Timetables
            |       +-- (optional) programmable logic conditions
            |
            +-- Rail reservations
            |       +-- Path signals
            |       +-- Dynamic consists
            |               +-- Shunting
            |                       +-- Yard programs (uses programmable logic)
            |
            +-- Cargo routing API
                    +-- Cargo demand
                    |       +-- (optional) shunting wagon requests
                    +-- Passenger destinations
                            +-- (optional) shared lines and timetables
```

## Hard links

| Feature | Requires |
| --- | --- |
| Any saved mod data | Versioned save container |
| Timetables | Shared lines |
| Path signals | Reservation map that understands wagons |
| Shunting | Consist editing on track, reservations, programmable logic |
| Yard programs | Shunting and programmable logic |
| Wagon requests between freight and yards | Cargo demand and shunting |
| Passenger transfers | Passenger destinations and shared lines |
| Larger maps or object caps | Save container, then NOF-aware object records |

## Must work without the other mod

| Feature | Does not require |
| --- | --- |
| Timetables | Shunting, cargo demand, passengers |
| Shunting | Passenger destinations, timetables, cargo demand |
| Shared lines | Path signals |
| Classic orders, block signals, depot coupling | Any mod |
| Cargo demand | Shunting. Requests are ignored if no yard mod is installed. |
| Vanilla SV5 play | The whole framework |

## Activation rules

Implemented for in-memory manifests:

- Duplicate ids fail.
- A missing hard dependency fails.
- A missing optional dependency is allowed.
- A present optional dependency is ordered first.
- A listed conflict that is also present fails.
- Cycles fail.
- An incompatible `engineApiVersion` fails at discovery.
- Any error clears the load order.

Not implemented: enable flags, per-save required mods, version ranges beyond API compatibility, or loading from `.olmod` zips.

## Engine capabilities

Mods name capabilities they need (`logic`, `lines`, `reservations`, `consists`, `cargo-routing`, `extended-save`, `extended-objects`, `large-world`). The loader currently stores the strings and does not check them against the engine. That check is a later milestone, before any gameplay mod is activated.
