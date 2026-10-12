# Dependencies

Hard dependencies are required to activate. Optional integrations add behaviour when the other mod is present and must not be required for the base feature.

```
Engine foundation
    |
    +-- Shared lines
    |       +-- Timetables
    |       +-- (optional) block rules on the line's orders
    |
    +-- Block rules on a vehicle's own orders
    |
    +-- Rail reservations
    |       +-- Path signals
    |       +-- Dynamic consists
    |               +-- Shunting
    |                       +-- Yard rules (same Rules window, later)
    |
    +-- Cargo routing API
            +-- Cargo demand
            |       +-- (optional) shunting wagon requests
            |       +-- (optional) destination check in the Rules palette
            +-- Passenger destinations
                    +-- (optional) shared lines and timetables
                    +-- (optional) "passengers going to" check in the Rules palette
```

## Hard links

| Feature | Requires |
| --- | --- |
| Any saved mod data in the future container | Versioned save container |
| Block rules on a line, once saved | The lines companion for the line, plus `<save>.SV5.orules` |
| Block rules on a private vehicle, once saved | `<save>.SV5.orules` |
| Timetables | Shared lines |
| Path signals | Reservation map that understands wagons |
| Shunting | Consist editing on track and reservations |
| Yard rules | Shunting and the Rules window |
| Wagon requests between freight and yards | Cargo demand and shunting |
| Passenger transfers | Passenger destinations and shared lines |
| Larger maps or object caps | Save container, then NOF-aware object records |

## Must work without the other mod

| Feature | Does not require |
| --- | --- |
| Timetables | Shunting, cargo demand, passengers |
| Shunting | Passenger destinations, timetables, cargo demand |
| Shared lines | Path signals, block rules |
| A vehicle's own orders | A shared line, block rules |
| Classic orders, block signals, depot coupling | Any later feature |
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
