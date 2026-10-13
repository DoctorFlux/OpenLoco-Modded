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

Arrival timing uses native land routing handles/subpositions and distance-based braking, native ship deceleration and airport arrival, plus the existing station cargo lifecycle. Planner waypoint/route-through progression is not physical arrival. Before-travel checks read the same current cargo/platform/timetable/vehicle data, with next-trip checks deferred until loading is complete. No prediction service is added. The optional v1 PHASES extension preserves per-vehicle approach and arrival decisions after line loading (D36/D37).

Clickable fields use the existing native Dropdown, TextInput, small font, Input ownership setters and scroll views. A shared field geometry/hit-test helper serves Rules and both ordinary order tabs. Paging avoids native menu storage/disable-mask overflow; it does not add a UI library. MakeCode remains a design reference, with no embedded Blockly runtime or new scripting subsystem. Dialog/menu state is transient; validated rule commands and the existing companions remain the dependencies (D39–D41).

Live shared-line map diagrams reuse the bounded rules interpreter with fresh control state, current station/vehicle/platform/timetable/year queries, and existing LinePath road/tram/rail/water/air searches. They depend on native UI updates for bounded polling and incremental geometry; no worker, external library, forecast service or new save dependency is added. Native topology command success invalidates geometry. Actual movement still uses its own validated rules and pathfinding; map state is read-only and transient (D43/D44/D46).

Stable Path/All publication uses only native per-line snapshot buffers, existing directed leg keys and the same bounded search queue. The build cache and published snapshots each have a 128-leg/131072-entry cap; staged legs are protected while replacement builds. Selected-line priority and stable admission stop All cache thrashing. No simulation dependency, thread, external library or persistence is added. Separate vehicle-progress projection reads the existing ready/current fields without advancing the live runner; map diagrams use current facts independently (D45/D46).

D46 separates whole-line current-condition diagrams from vehicle-progress projections. Path/All uses fresh interpreter state and the selected head only for facts; it no longer depends on physical position, current order, saved branch choices or counters. Existing condition queries, bounded searches and publication buffers suffice. No new dependency or save field is introduced.

D47 adds only transient view-versus-geometry invalidation and a per-plan rebuild flag. Existing topology commands still invalidate searches, while native line metadata edits reuse cached geometry. Retaining publications requires no extra snapshot store, worker, new cap or simulation dependency. Explicit Rules/Lines resets clear these buffers immediately.

D48 waypoint reachability reuses native current orders, track connections, the bounded PathSearch graph, physical vehicle-tile scanning and atomic reservation holds. The red waiting display reuses native signal-object frames and viewport invalidation. Waiting-owner sets and required-target request flags are transient; no library, mod loader, save-layout or external service dependency is added.
