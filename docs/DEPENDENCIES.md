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
    |                       +-- Yard rules (same inline Rules editor, later)
    |
    +-- CargoDist packets and routing
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
| Passenger transfers | CargoDist compatible service graph; private orders or shared lines |
| CargoDist persistence | Matching native save and `.ocargodist` v1; line/rules companions when referenced |
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

Private rules need only vanilla orders and vehicle state. Shared rules bind to Lines and load after its companion. Late/on-time checks require a matching timetable slot. Platform-free checks require a rail list and connected station tracks; physical occupied holds include wagons, while reservations are ignored. Missing check data is unavailable and selects the following Else if/Else branch. Existing checks need no destination or consist subsystem. D62/D63 adds destination checks backed by CargoDist; consist/yard blocks remain hidden.

Else chains, Forever repeats and Break need no additional subsystem. Break validates its enclosing Repeat; the shared editor layout and OrderManager display-frame refresh serve private and shared order lists. Existing save companions and packed orders keep their versions.

Container drops and MakeCode-style shapes use the existing rules model, command validation and native DrawingContext. They add no external editor dependency. Explicit membership lives only in commands; stable expanded bounds use the existing rules companion.

Arrival timing uses native land routing handles/subpositions and distance-based braking, native ship deceleration and airport arrival, plus the existing station cargo lifecycle. Planner waypoint/route-through progression is not physical arrival. Before-travel checks read the same current cargo/platform/timetable/vehicle data, with next-trip checks deferred until loading is complete. No prediction service is added. The optional v1 PHASES extension preserves per-vehicle approach and arrival decisions after line loading (D36/D37).

Clickable fields use the existing native Dropdown, TextInput, small font, Input ownership setters and scroll views. A shared field geometry/hit-test helper serves Rules and both ordinary order tabs. Paging avoids native menu storage/disable-mask overflow; it does not add a UI library. MakeCode remains a design reference, with no embedded Blockly runtime or new scripting subsystem. Dialog/menu state is transient; validated rule commands and the existing companions remain the dependencies (D39–D41).

Live shared-line map diagrams reuse the bounded rules interpreter with fresh control state, current station/vehicle/platform/timetable/year queries, and existing LinePath road/tram/rail/water/air searches. They depend on native UI updates for bounded polling and incremental geometry; no worker, external library, forecast service or new save dependency is added. Native topology command success invalidates geometry. Actual movement still uses its own validated rules and pathfinding; map state is read-only and transient (D43/D44/D46).

Stable Path/All publication uses only native per-line snapshot buffers, existing directed leg keys and the same bounded search queue. The build cache and published snapshots each have a 128-leg/131072-entry cap; staged legs are protected while replacement builds. Selected-line priority and stable admission stop All cache thrashing. No simulation dependency, thread, external library or persistence is added. Separate vehicle-progress projection reads the existing ready/current fields without advancing the live runner; map diagrams use current facts independently (D45/D46).

D46 separates whole-line current-condition diagrams from vehicle-progress projections. Path/All uses fresh interpreter state and the selected head only for facts; it no longer depends on physical position, current order, saved branch choices or counters. Existing condition queries, bounded searches and publication buffers suffice. No new dependency or save field is introduced.

D47 adds only transient view-versus-geometry invalidation and a per-plan rebuild flag. Existing topology commands still invalidate searches, while native line metadata edits reuse cached geometry. Retaining publications requires no extra snapshot store, worker, new cap or simulation dependency. Explicit Rules/Lines resets clear these buffers immediately.

D48 waypoint reachability reuses native current orders, track connections, the bounded PathSearch graph, physical vehicle-tile scanning and atomic reservation holds. The red waiting display reuses native signal-object frames and viewport invalidation. Waiting-owner sets and required-target request flags are transient; no library, mod loader, save-layout or external service dependency is added.

D49 reuses native track entry directions, existing reservation checks and VehicleHead passing-colour functions. The ancestry prefilter uses the standard library; no new library, thread or UI dependency is added. Ordinary unsignalled junctions retain their current pathing policy unless the vehicle already uses path mode.

D52 approaching signal aspects reuse physical vehicle-front track locations, committed routing slots, native connections, the existing bounded waypoint Path/Chain search and the signal painter's red frame. Separate approaching/waiting owner sets and 16-tick probe timers are transient; they create no reservations, new saved train records, external service, worker, UI library or companion-version dependency. Real reservation waits skip the additional search.

D53 corrects native danger/proceed state conversion and reuses the same waypoint search at unplanned signalled junctions. Original art was inspected read-only for evidence; it is neither a new shipped asset nor a test/runtime dependency. No new library, service, worker, order/save field or companion version is introduced.

D54 route admission reuses existing bounded pending steps, vehicle-tile occupation checks, hold conflict validation, wait notifications and reservation retry timing. It adds no graph search during pending validation, new library, worker, UI dependency or persistence field. Special-signal overrides use the same existing reservation search as ordinary special-signal passage.

D54 physical-entry protection uses the native Vehicle2 track-piece transition, the same physical occupation query and existing protected hold lifetime. It adds one piece check at physical entry, without another path search or new saved state. Unprotected ordinary movement remains native.

D54 placement isolation (`023ec618`): native temporary placement motion uses the existing non-simulation flag and its own validation; it must not add physical reservation waits. A regression first failed on `ab57d310`, then passed with the guard restricted to the live Vehicle2 simulation path. No additional saved or UI state is introduced.


D55 occupation uses local native track elements, physical entity tile lists, quarter-tile conflicts and immutable native movement tables. A fixed 176-entry footprint offset cache includes both directions and endpoint overhangs; it has no per-game entities, new persistence, worker, global vehicle scan, recursive expansion or extra search dependency. Live entry safety continues through ordinary posts while the existing rail-logic flag is active.

D56 adds no dependency. Bounded PathSearch target-reached state keeps safety checks running past route-through waypoints; native rail waypoint advancement releases unused continuation claims through the existing Signals API while retaining occupied holds. Physical wagon/entity queries, foreign claim validation and native red display are unchanged.

D57 supersedes the standalone-window/small-font UI dependency description above. Inline editing uses the existing native medium fonts and glyph measurements, Dropdown/TextInput, widget/window events, scroll areas and validated Rules commands. Each window owns its editor session; the shared-line browser plus route and palette need three UI scroll areas. Captured drag continuation is opt-in for the two order hosts. No external toolkit, browser, Blockly runtime, scripting language or new persistence dependency is introduced.

D57 regression verification also covers native scroll-index lookup for adjacent browser/order panes: the previous pre-incremented widget index selected the preceding pane. The corrected lookup includes widget zero, preserves the scrollbar-to-content mapping and is required for the three-pane editor.

## CargoDist delivery (D62/D63)

CargoDist depends on native cargo production/compartments/acceptance/economy and the existing order model. Shared lines and Rules provide optional effective-order integrations; private services work independently. Consists, shunting and the save container are not prerequisites. `.ocargodist` loads after lines/signals/rules and validates native holder identities/amounts. Destination checks use this shared subsystem and existing stop keys. Native medium-font widgets, dropdown paging and per-window state need no external UI, scripting or network dependency. Computational and display work is bounded; motion and reservation ownership are unchanged.

D65 automatic-transfer follow-up supersedes standalone CargoDist windows and congestion overlays. Use normal StopAt/private/shared/optional-rule routes and base-game combined StationIds. Inline Cargo-tab summaries and the per-game Options Misc Automatic/Classic preset replace the large UI; mixed modes and hidden settings persist. Reached-stop ownership/key/revision/signature context and UI caches are transient and reconstruct after load, preserving cargo when unresolved. Packed saves and companion versions remain unchanged. CargoDist remains TESTING pending a short user coal/passenger/save check; no consist/shunting or new save-container dependency is introduced.
