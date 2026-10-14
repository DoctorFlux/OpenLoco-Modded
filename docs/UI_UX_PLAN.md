# UI and UX

D68 accepts the delivered CargoDist UI for master integration: normal orders/shared lines/optional Rules, compact expandable station/vehicle destination summaries, Automatic/Classic in Options Misc and no automatic-origin wording. The D62/D63 standalone windows and congestion overlays remain superseded by D65/D67. No new layout or controls are added during integration; historical CargoDist TESTING gates are superseded.

D67 automatic Cargo entries show native quantities followed by distribution/destination summaries; omit the legacy station "en-route from" line and vehicle "from [station]" suffix. Station base rows return to 12 pixels, with destination lines using the existing shared variable-row geometry. Vehicle image/compartment geometry and controls remain. Classic entries retain native presentation. Origin metadata is not changed by drawing.

D66 keeps the D65 compact interface. One native cargo entry can expand several final station destinations, including different stops within one town. A combined station's cargo pool is shared across its facilities; an ordinary nonaccepting interchange uses the same summary. Use normal orders/lines and optional Rules, with Automatic/Classic in Options Misc. No warehouse designation, transfer-order button or new destination window is added. Feeder credit contributes to existing vehicle/shared-line profit reporting; company cash follows final delivery. Test evidence and user-only acceptance are in development status.

## Style

Use the existing OpenLoco window widgets, colours, and string ids. Do not add a separate toolkit. Windows should look like the vehicle and station windows players already know.

The Rules button sits in the vehicle Route and shared-line Orders controls. It expands that same window into the inline editor; no separate Rules window opens. It is specified in `docs/mods/PROGRAMMABLE_LOGIC.md`.

## Windows to add

| Window | Opens from | Shows |
| --- | --- | --- |
| Mod manager | Removed | The title-screen Mods button and the mod manager window are gone |
| Line manager | Click the in-game top toolbar Lines button, or Options > Miscellaneous | Normal window colours. Orders tab with numbered orders, slots, and Local/Express. Vehicles tab can add or buy a vehicle. The vehicle route tab keeps the normal order list and has a Line button |
| Gameplay options | Options > Miscellaneous | Existing disable-breakdowns checkbox. Added no obsolescence, infinite money, and a calendar speed dropdown (Normal, Slow, Frozen). These are engine settings in `openloco.yml`, not installable mods |
| Mod config | Mod manager | Settings declared by the mod schema |
| Shared lines | Vehicle or a lines button | Line name, mode, stops, assigned vehicles, status |
| Timetable | A line | Interval, dwell, targets, lateness |
| Rules | Normal Rules button in Vehicle Route or shared-line Orders | Inline editor with independently scrolling 88-pixel palette, compact native C blocks, directly editable medium-font fields, Actions/Details/bin, multi-order dragging/resizing and progress. Shared-line browser stays visible. Spec: `docs/mods/PROGRAMMABLE_LOGIC.md` |
| Yard controller | A configured yard | Track roles, the shared Rules editor, job queue |
| Consist | A train | Order of vehicles, couplers, which end is powered |
| Wagon requests | A freight line or yard | Open requests and assigned jobs |
| Path signal | Signal tab checkboxes in rail construction | Path and chain checks on the normal signal, plus the existing one-way and two-way buttons. Arrows: white two-way path, yellow one-way path, red/blue/green chain aspect |
| Reservation viewer | Train window main tab | One pressed button shows that train's reserved path on the map |
| Passenger demand | Station | Groups waiting, destinations, connections |
| Cargo demand | Industry and station | Supply, demand, waiting loads |
| World generation | Scenario editor | Size, town spacing, industry density, terrain |
| Object selection | Scenario editor | Search, category, filter, sort, presets, missing objects |

## Behaviour

- Every failed command shows a specific reason: blocked, no path, incompatible wagon, missing dependency, full, timeout.
- Highlight the train, wagon group, or track under discussion. Do not rely on ids alone.
- Destructive actions (discard mod state, delete a yard) name the save file they will write. They never overwrite the original SV5.
- Disable buttons that do not apply to the transport mode. Ships do not get coupler controls.
- Words on a block are the words in the tooltip.
- Rules expands the current window, clamped to the logical screen. Defaults are 560x320 (vehicle) and 640x320 (line); editing minimums are 400x240 and 560x268. Closing editing restores the prior normal size. Native windows remain movable; no overlay panel covers existing widgets (D57).
- The vehicle route column already holds unload, wait, skip, delete, up, down, and reverse. The line orders tab already holds Local, Express, Loop, the timetable slot, and auto spacing. Rules does not take those widgets.

## Current build

Rules has native widgets and is in TESTING. The mod manager described in older notes is gone.

## Current inline editor (2026-10-13, D57, TESTING)

On `codex/native-inline-rules`, the tab-shaped vehicle entry and separate window are replaced by direct editing in the existing order windows. Keep the shared-line browser visible. Medium condition text uses actual font measurements; 20-pixel headers, 14-pixel orders and 8-pixel closing arms reduce wasted space. Muted native colours, bevels and shallow C connectors preserve Locomotion presentation. Thin append strips replace permanent drop-inside text rows. Colour feedback and both progress indicators remain.

Each host owns an EditorSession, with independent selection, drag and guarded field callbacks. A third transient native scroll area accommodates the line browser, orders and palette; only these hosts opt into captured scroll motion outside the pressed pane. Map tools, validated commands, runtime and save formats remain as before. The palette, optional Details, Actions/bin, caption, existing controls and two progress footers have distinct layout regions. Final build/test evidence and the user's short visual/edit/drag acceptance belong in development status.

## Earlier block-rules implementation (2026-10-12, historical)

The following records the former separate-window workflow. D57 above supersedes its entry, placement, font/row metrics and singleton interaction state.

The native Rules subsystem is implemented on `codex/block-logic-programming`. See `mods/PROGRAMMABLE_LOGIC.md` for current controls, limits, runtime and companion behavior; see `DEVELOPMENT_STATUS.md` for actual verification and remaining manual checks. Geometry supports default 560x320 and compact 360x240, with a pinned palette, scrolling list, section grips, bracket dragging, deletion bin, and mouse-following dragged-item label. Commands validate before simulation mutation. No map tool is claimed by Rules dragging.

Cargo checks offer simple has/no-cargo presets, a Stop selector, a Cargo selector and quantity comparisons. Vehicle boolean checks disable numeric controls. Wait has separate Value and Minutes; Repeat uses Times. Block sentences wrap; controls fit every supported size. Valid drops use the model validator before showing a position. Escape and outside drops cancel, and the footer reports faults or current execution. The manual matrix remains in `DEVELOPMENT_STATUS.md`.

Rule sections now resize with First row/Last row and the selected bracket's end grip. The vehicle Route and line Orders tabs show actual wrapped block headers and nested C-shaped brackets when blocks exist. Clicking a header opens that exact block in Rules. All three views share the variable layout used for hit testing and scrolling. Stops/waypoints drag directly in those lists as well as Rules; previews, valid insertion positions and outside/Escape cancellation use validated model edits. Recorded true/false conditions render green/red; active markers show program progress, alongside a separate next-stop/waypoint footer. Pending decisions show a provisional next listed row. A line's footer identifies the vehicle whose independent execution is shown. The clock occupies free space next to Schemes rather than drawing over its button. Structural edits refresh active white map-number circles. Else/Else if replace the old Otherwise wording; Repeat offers Forever and Break selects an enclosing loop. The compact palette uses two columns when needed. These latest controls await user-run in-game checks.

Container insertion now uses a visible `+ drop inside` cavity slot and the exact nested `=` grip coordinates. Native shapes follow the linked MakeCode reference: rounded puzzle connectors, full C arms, hexagonal condition sockets and rounded value fields. Palette and drag previews share category colours; existing green/red recorded decisions remain visible. Adjacent Else arms join their preceding branch. The append-slot geometry is shared with normal vehicle/shared-line lists. User feedback reported container insertion failing; regression tests now cover the fix and visual/manual recheck remains pending.

Only Wait blocks expose **When**: Automatic, Before travelling, On arrival. Group keeps Name; the corresponding Details button is hidden for other kinds. Ordinary branches/actions/loops have no timing socket or sentence suffix in Rules, vehicle Route, line Orders or pointer previews (D42). Existing saved timing is still honored. Ordinary automatic decisions choose a trip using current facts; saved arrival branches still check after reaching their anchor. Wait normally holds at the final routable row; its before-travel setting holds at the preceding boundary. The footer distinguishes approaching an arrival point from an active wait. Layout and map-tool ownership are unchanged. Native stopping and the Wait timing control await user manual acceptance.

The white route arrow is drawn after row labels and selection shading in the nested order's left gutter. Its dark edge keeps it visible over block colours. Vehicle Route, shared-line Orders and Rules use the same stable committed-travel/hold key and row geometry, including scroll clipping. Cargo scans and pending choices do not move it onto an unselected destination; during station cargo handling it stays on the reached stop until departure. Active block/condition feedback and the next-listed footer remain separate. This view never advances rules or changes simulation state. User manual visual recheck is pending (D38).

Direct editing now uses visible sockets in the shared block layout. Rules opens native dropdowns/text dialogs from the clicked socket; normal Vehicle Route and shared-line Orders clicks open Rules on that exact field. The primary and secondary checks use distinct rectangles. Small native field text, wrapping, 16-pixel order bodies and 18-pixel closing slots keep nested content compact. The default is 560x320, minimum 360x240. Details is initially hidden, restoring 58 pixels of canvas; Actions provides Else if/Else/removal. Palette clicks add to the selected scope. The toolbar, caption, canvas, optional control rows, bin and progress footers occupy separate rows. Long native selectors page at 30 choices plus navigation; callbacks check owner/key/revision. Editing retains scroll position, full name dialog values and live waits on unchanged input. This uses existing native UI and requires user visual/popup acceptance (D39–D41).

Manual Skip keeps the outlined white arrow visible immediately in Vehicle Route and Rules, even before the next simulation tick. Plain Rules lists use their displayed keyed snapshot; cargo cursors advance the marker to the next routable row and End wraps it. Existing physical holds take priority. Shared-line Path/All uses the line window's progress vehicle as its visible rule source and builds current-choice route geometry incrementally and replaces the displayed line atomically. The previous complete route remains visible with Updating feedback until replacement is ready; incomplete branch mixtures are never displayed. The existing clipped status footer reports building/updating, waits, missing data or preview limits; no extra window/control row is added. Full numbered world circles retain their order identities when inactive route branches disappear. Selected-line colours remain dominant across static and live layers. Manual appearance/responsiveness acceptance is pending (D43/D44).

Path/All shows the complete current-condition route independently of the progress arrow (D46). The footer says `Map: current conditions`; existing Updating/Wait/limit messages retain priority. If passengers at stop 1 are zero, an If section containing stop 1 and waypoint 2 is absent from the diagram and only Else waypoint 3 plus unconditional waypoint 4 connect. Positive cargo removes waypoint 3 instead. Moving to another order does not rebuild unchanged geometry. All three white arrows still identify the actual travel/hold destination. No additional controls or layout changes are added.

D47 retains the completed line during construction-related Updating intervals, rather than making it disappear while searches rebuild. Recolouring updates the visible cached route immediately; hidden-line skips and edits do not blank it. The existing footer and layout suffice. Closing the window or switching game/save clears the display immediately. Native appearance remains a user manual acceptance item.

D48 changes existing special-signal feedback without adding widgets: while an approach reservation fails, that path/chain/combined head displays its red frame. The override is read-only and leaves native occupancy/animation and route lights intact; it clears back to the native frame when no train waits there. Block signals and ghosts retain native frames. Multiple waiting trains share the red indication without one clearing another's wait. Native object appearance remains a user manual check.

D49 adds no controls: Else/Next waypoint 3 feedback retains its existing meaning. A blocked outer approach now keeps the incoming train waiting red rather than approving the inner circle; selected special junction arms use the same native passing colours as single connections. The supplied screenshot layout still needs the user's final native recheck.

D57 regression verification also covers native scroll-index lookup for adjacent browser/order panes: the previous pre-incremented widget index selected the preceding pane. The corrected lookup includes widget zero, preserves the scrollbar-to-content mapping and is required for the three-pane editor.

Inline geometry changes force thumb recalculation for both editing panes, including the third shared-line palette, and clamp expanded content offsets. Changing the window size or showing/hiding Details therefore keeps scrolling consistent with the drawn viewport.


D58 (2026-10-14) refines the inline editor: only logic uses shaped blocks. Stops and waypoints sit on the ordinary list background with native medium-bold captions and white circle numbers, rather than rounded puzzle bodies or text numbering. Selection uses the native flat black highlight/white text. Drag grips stay in the margin, nesting and both progress markers remain, and shared orders display native timetable slot notes. Larger condition text is unchanged. User visual acceptance remains pending.

D59 (2026-10-14) records user acceptance of the delivered step and explicit approval to merge the complete logic branch into master. Phase 4 is COMPLETED (user accepted); this supersedes the earlier inline UI TESTING gate. Automated evidence remains 447/447 Debug/Release, focused Rules 182/182. Individual historical gameplay checks are retained as unitemized, not newly claimed as passed.

## Compact automatic-transfer UI (COMPLETED, D68)

D65 supersedes D62/D63 standalone CargoDist windows and all Cargo congestion overlays. There is no Map > Cargo, Destinations or line Capacity entry. Existing Path/All route display is retained.

Station/vehicle Cargo tabs keep native dimensions, sprites, quantity captions, empty rows, catchment/refit controls and other tabs. Automatic cargo adds one destination/transfer summary beneath each nonempty entry. Clicking the row expands/collapses destination quantities inline, with the existing scrollbar. Classic rows retain their native geometry. All draw, hover, hit and scroll positions come from the same cached CargoRows layout; per-window expansion clears on closure, owner change or game reset. Native normal-medium text is clipped to available width, never overlaid on controls. Queries/drawing are read-only and bounded; pending/unavailable routing displays Finding route/Waiting for connection.

Options > Miscellaneous places Cargo transfers beside Lines in the existing 420×356 layout. Automatic/Classic apply validated per-game settings. Mixed saved modes display disabled Custom (saved); opening the control never overwrites them. Advanced saved values remain hidden and preserved. Menus retain game/company/settings guards. Rules destination fields are unchanged. D68 records user delivery acceptance; future visual regressions remain user-controlled.
