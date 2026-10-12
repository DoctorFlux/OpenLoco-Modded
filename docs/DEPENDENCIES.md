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

## Package activation

The original in-memory manifest/loader implementation was removed (D23). There are no active manifest, enable-flag, directory-loader, or `.olmod` activation rules. The dependencies above describe native subsystem requirements and future feature gating.

## Engine capabilities

Mods name capabilities they need (`logic`, `lines`, `reservations`, `consists`, `cargo-routing`, `extended-save`, `extended-objects`, `large-world`). The loader was removed by D23. These are subsystem dependency names, not an implemented package activation API.

## Implemented block-rule dependencies

Private rules need only vanilla orders and vehicle state. Shared rules bind to Lines and load after its companion. Late/on-time checks require a matching timetable slot. Platform-free checks require a rail list and connected station tracks; physical occupied holds include wagons, while reservations are ignored. Missing check data is unavailable and selects the following Else if/Else branch. No destination or consist subsystem is required; those later blocks stay hidden.

Else chains, Forever repeats and Break need no additional subsystem. Break validates its enclosing Repeat; the shared editor layout and OrderManager display-frame refresh serve private and shared order lists. Existing save companions and packed orders keep their versions.

Container drops and MakeCode-style shapes use the existing rules model, command validation and native DrawingContext. They add no external editor dependency. Explicit membership lives only in commands; stable expanded bounds use the existing rules companion.
