# Block rules

The first block-rules delivery is **COMPLETED (user accepted D59/D61)** and included in release `v26.09-modded.3`. D71 (2026-10-23) adds the dragging/conditional-jump follow-up directly on master at the user's request; its current verification and TESTING gate are recorded in `DEVELOPMENT_STATUS.md`. Earlier branch/window/rollback descriptions below are historical; modded.1/.2 releases and tags were retired by D69.

D71 automated verification: final source `04b8a699`, both game builds successful, **583/583 full tests per configuration**, including 199 Rules checks. Status **TESTING** for a short user-only caption-drop and conditional-route check. The published .3 executable is unchanged; use the rebuilt development executable.

## Order dragging and conditional orders (D71)

Drag a stop/waypoint or cargo-instruction caption, or its `=` grip, onto an order, block header or thin append strip to move it inside that block. Upper/lower row halves choose before/after. Explicit interior drops grow the bracket, including its enclosing containers. Shift-drag selects consecutive orders; dragging a selected caption then moves that selection. Header dragging still moves a complete section, and the closing grip resizes it. Stop/waypoint captions remain native. This fixes the old caption-drag selection behavior that made interior moves appear unavailable. All existing stale/outside/owner/readonly cancellations and atomic command validation remain.

The palette also offers **Jump if**, following the [OpenTTD conditional-order concept](https://wiki.openttd.org/en/Manual/Conditional%20Orders). Insert it before an existing order, click its check/comparison/value and target, and choose a stop/waypoint already on the list. True chooses the target; false/unavailable continues to the anchored order. It is not a C-shaped container and does not consume a packed order slot. It uses an existing Go to record with no branch parent, its own condition and a single-order anchor; stable target keys prevent retargeting after edits. Missing destinations disable the action with an explanation. A branch-attached Go to retains its existing unconditional behavior. Plain Go to outside a branch defaults to Always; Jump if can be independently nested inside an If/Else/Repeat.

All existing checks are available, plus **load percentage**, **vehicle age in completed years** (oldest car), **native aggregate reliability (%)**, **maximum speed (mph)** and **Always**. Numeric comparisons are `<`, `=`, `>`, `!=`, `<=`, `>=`; boolean checks offer true/false. Load includes usable primary and secondary compartments; a vehicle without capacity is unavailable. Native production, movement, loading and signal safety remain unchanged. Conditional cycles share the existing 256-step tick budget, retain reached-order progress and resume next tick; shared vehicles evaluate their own facts. CargoDist considers both possible outcomes for dynamic checks, but resolves constant Always true/false choices without advertising impossible services. Always needs no selected vehicle. Live route previews use actual current conditions.

This is native behavior inspired by OpenTTD, independently implemented in this MIT project. It does not import OpenTTD code/saves or add depot orders, depot servicing, remaining-lifetime, maximum-reliability or driving-backwards conditions. The latter need separately defined native semantics. New check/comparison values append to existing enums in `.orules` v1, preserving old values and old companions; older executables cannot read newly used values. Keep copies and matching companions when switching to the published .3 checkpoint.

The player drags blocks onto an order list, in the style of Microsoft MakeCode. The words on the blocks are short enough for a child to read. There is no text language, no Lua, and no WebAssembly. Decision D26.

A list with no blocks is the current game. A vehicle with its own orders keeps those orders. A shared line with no blocks keeps the line it has today.

## What the first build includes

The delivered build is the blocks, the inline Rules editor, and those blocks running on both of these lists:

- the orders of one vehicle that is not on a line
- the orders of one shared line, for every vehicle assigned to that line

A block can wrap one row, or a whole stretch of rows. A stretch can hold stations, route-through orders, and waypoints together.

Yard, shunter, cargo-destination, and passenger-destination blocks use this same window later. They stay out of the palette until those features exist. The first build is still the whole order-rules feature for vehicles and shared lines, not a runner with nowhere to put a block.

## The order list

The rows stay the vanilla orders: end, stop at, route through, waypoint, unload all, and wait for a full load. Blocks sit beside that list. They are not new values in the order type.

`Order::_type` keeps its three type bits. The two unused type values stay unused. `sizeof(Order)` stays 1 for the base order. A wider order does not fit the save, and block rules do not ask it to. See `ENGINE_LIMITS.md`.

Each order that rules can see has a key owned by the rules data. The key is not stored in the order byte. Insert, delete, and move update the key map in the same command as the order change. A block stores keys, so adding a stop above a group does not retarget the group.

The end order is the bottom of the list. A block cannot wrap it, and Go to cannot target it.

The existing vehicle button that skips the next order once stays that button. Block Skip is a rule in this window. The two are different controls.

## Blocks

The palette has these blocks:

| Block | On the list | What the player does |
| --- | --- | --- |
| If | A C-shape around one row or a group | Reads one check. The rows inside run when the check passes |
| Else if | A C-shape after If or Else if | Checks another condition only when no preceding branch passed |
| Else | A C-shape after If or Else if | Runs this stretch when no preceding branch passed |
| And / Or | One extra sentence on that If | Joins a second check. An If has one extra sentence at most |
| Repeat | A C-shape around one row or a group | Runs that stretch 1–20 times, or Forever |
| Break | Inside an enclosing Repeat, optionally inside a branch | Leaves the selected loop and continues after its last row |
| Wait until | On one stop, waypoint, or group | Holds there until the check passes, or for a number of minutes the player sets |
| Skip | Inside If, Else if or Else | Misses that row, or the whole group, on this pass |
| Go to | Inside If, Else if or Else | The next order is a stop, route-through, or waypoint already on this list |
| Group | A bracket beside a run of rows | Names one stretch so If, Repeat, Skip, and Go to can use it |

The player fills a check from sentences, not from typed code:

- more than
- less than
- the same as

Numbers are small and typed in a box on the block. The rest is a dropdown.

Checks in the first palette, because the game can already answer them:

- Cargo waiting at a named stop on this list is more than, less than, or the same as a number
- This vehicle is full, empty, or has space
- The platform at a named stop on this list is free. This reads physical occupation of connected station track pieces, including wagons, excluding the evaluating train. Reservations alone are not occupation; it does not select a platform
- This vehicle is late, or on time, at a stop that has a timetable slot
- The year is before or after a chosen year

A late or on-time check with no timetable slot on that stop takes the Else rows. The Rules footer says the check is unavailable. An unavailable check does not count as passed.

These checks are named now and hidden until their feature exists. They are the same kind of sentence, added to this palette later:

- passengers going to a station
- freight for a destination
- a siding is occupied
- a path is reserved
- a shunter is free

Couple, uncouple, split, and the other yard actions are later blocks in this palette. The rail-only ones stay hidden on a road, sea, or air list.

## A stretch of the route

A group is a contiguous run of orders. It can contain stop-at, route-through, waypoint, unload-all, and wait-for rows. Waypoints count. The player can bracket three stations, or a station, the waypoint after it, and the next station.

Skip on a group misses every row in the bracket, including the waypoints. Go to a row inside a group starts at that row. Repeat on a group runs the whole bracket, then runs it again, up to the number on the block.

Repeat's Times dropdown offers Forever and 1–20. Break's Loop selector chooses an enclosing Repeat; it can run unconditionally or when its If / Else if / Else branch is taken. Reaching the group again later starts a fresh count. Two vehicles on one line each keep their own count.

An order inserted between two members of a group joins the group. An order inserted above or below the group stays outside it. Dragging one row out of the bracket takes it out of the group. A group that would be left empty is removed.

If, Else if, Else, and Repeat can wrap a group. They can also wrap a single row. An If can sit inside a Repeat. The limit is four C-shapes around one row. A deeper drop is refused.

## Two examples

List: Town, Mill, waypoint, Port, Depot. No blocks. The vehicle visits Town, Mill, the waypoint, Port, and Depot, then carries on as the line or the vehicle orders already do.

Same list. A group brackets Mill, the waypoint, and Port. The If on that group says cargo waiting at Mill is less than 8, and the inside is Skip. When fewer than 8 loads are waiting, the vehicle goes from Town to Depot and misses Mill, the waypoint, and Port. When 8 or more are waiting, it visits Mill, the waypoint, and Port, then Depot.

Same list. An If says this vehicle is full, and the inside is Go to Depot. Else brackets Mill, the waypoint, and Port. A full vehicle's next order is Depot. A vehicle that is not full does Mill, the waypoint, and Port.

The stops stay on the list either way. Skipping does not delete them.

## Inline Rules editor (D57)

The player edits blocks directly in the vehicle Route or shared-line Orders window. A normal **Rules** button in the order controls toggles the palette and tools; there is no separate Rules window or tab-shaped Rules entry. Field clicks open native dropdowns or value/name dialogs in the originating window even when the palette is closed. Clicking a header selects its section instead of opening another window.

Opening editing expands that same window to 560x320 for a vehicle or 640x320 for a shared line, preserving a larger existing size. Minimum editing sizes are 400x240 and 560x268; the maximum is 1000x700, constrained to the logical screen. Closing editing restores the previous normal size. Leaving Route/Orders cancels editing and unfinished interactions. The shared-line browser remains visible. Vehicle editing of assigned orders explicitly says **Editing shared line: [name]**; edits affect that line. Private blocks are not copied onto it.

An 88-pixel palette scrolls independently beside the order canvas. Actions provides Else if, Else and block removal. Details is initially hidden and shows applicable check/value/section controls in responsive rows. The deletion bin removes a block while preserving its orders, or deletes the dragged order selection through validated commands. Existing Local/Express, line assignment, route controls, timetable/spacing, colours and Schemes remain available. The timetable clock retains its own space beside Schemes.

C-shaped blocks use native Locomotion palette shades, shallow connectors, bevels and medium text. Condition fields use the normal medium font; headings use medium bold. Widths come from native glyph metrics. Single-line headers are 20 pixels (16 without fields), sockets 16, order rows 14, and closing arms 8. Narrow headers wrap without shrinking their font. Full field/block information remains available through hover text. The former permanent `+ drop inside` text row becomes a thin highlighted append strip; section end grips remain reachable. Ordinary lists with no blocks and the palette closed retain their native 10-pixel order rows.

Stops, route-through orders and waypoints retain the base-game list appearance even inside a control block: native medium-bold order captions on the list background, white number-circle sprites and flat selection highlighting. They have no puzzle silhouette or textual ordinal prefix. The left margin keeps drag grips and nesting; shared-line orders also retain their native timetable slot notes. Logic headers/sockets remain shaped and coloured (D58).

Drag blocks from the palette, individual stops/waypoints, consecutive selections, or a complete header section. The selected end grip resizes a section across multiple orders. Drag previews follow the pointer and valid destinations are highlighted using model validation. Drops into an append strip explicitly grow that container. Invalid drops, the map, another window, Escape, closing the window, changed ownership and intervening edits cancel without mutation. Captured scroll input handles palette-to-canvas dragging without claiming the map tool. Genuine map clicks retain native stop/waypoint insertion. Structural edits refresh active white map-number circles.

Each window owns its editor state; multiple vehicle windows cannot share selections, drags or pending field callbacks. Native menus retain pagination and owner/block/revision guards. Unchanged edits preserve live runtime. Missing or incompatible choices and rejected edits report a specific reason. Read-only vehicle lists cannot change blocks.

Both order views retain green/red branch feedback, current-program markers, the outlined white committed travel/hold arrow and separate program/status and next-destination footer lines. Shared-line progress identifies its representative vehicle. Unfinished decisions still show a provisional next listed destination. Rules execution, native movement, signal safety and all save versions are unchanged.

D57 supersedes the separate-window entry, placement and singleton editing workflow in D29/D39/D40. Earlier implementation/verification sections below are historical snapshots. Final native appearance and interaction acceptance remains user-run; keep TESTING.

## How a vehicle runs the blocks

Ordinary If/Else if/Else, Repeat, Skip, Go to and Break blocks have no arrival/before field or label. New blocks use automatic timing: branches, loops and standalone Break choose the next trip; branch actions follow their parent. Group remains a name and bracket. Only **Wait until** exposes **When**: Automatic (on arrival), Before travelling, or On arrival. The Wait field and its Details button edit the same setting. Existing saved explicit timing remains supported without rewriting the route (D42).

Before travelling reads the current state before choosing the next trip. For example, test cargo waiting at Mill before heading there, then skip Mill and its waypoints if there is no cargo. These are current facts about a named future stop; no forecast of cargo, occupancy or timetable arrival time is calculated. Existing saved On arrival decisions physically visit the section's first routable row before evaluating. A failed arrival If can skip the remaining rows; it cannot undo the stop already visited. A cargo-only section uses the preceding routable row, including wrap-around. An arrival action can divert or break a loop after reaching that anchor. Subsequent iterations need a fresh physical visit for their arrival checks.

Wait until normally holds at the last stop/route-through/waypoint in its section. The land route planner stops extending the route at that point; the physical train or road vehicle catches up using the existing distance-based braking pipeline. Speed is not set to zero by the rule guard. Only when the physical front reaches the planner's routing position and stops does the wait start its timeout. Ships use their existing gradual deceleration at waypoint targets; aircraft retain their normal airport arrival. Before-travel Wait holds at the previous safe route boundary before entering its section; if the land planner has reached that boundary ahead of the physical vehicle, it finishes braking there before starting the clock.

At a station, arrival checks run before loading/unloading changes their inputs, and branch choices remain fixed through the section. Cargo handling continues during station waits. Decisions for the next trip are deferred until loading completes, so a vehicle-full check before departure sees the loaded vehicle. Wait and unfinished decisions retain their current boundary and prevent travel toward a new unselected destination. Planner progression is explicitly separate from physical arrival; merely planning past a waypoint does not satisfy an arrival check.

When a vehicle is ready for the next order, the runner looks at the blocks on that order and on any group that contains it. The runner then chooses the next order. Movement, loading, unloading, and pathfinding stay the vanilla ones. Go to does not teleport. Skip does not remove the row.

The host runs at most 256 block steps for one vehicle on one tick. A decision that needs more steps waits until the next tick. Wait until arms the check and stops spending steps until the next tick. While a vehicle waits at a station, cargo still loads and unloads, as it does for a timetable hold. A wait on a waypoint holds on that waypoint.

Minutes on a Wait until block are timetable minutes: 40 simulation ticks. They do not read the wall clock, and a frozen calendar does not freeze them.

A fault records the block and a reason. The vehicle keeps the order it already reached. A command is atomic.

The same orders and the same checks produce the same next order. There is no random block.

A vehicle on a line uses the line's blocks. Each vehicle stores its own repeat count and its own wait. Editing Rules while that vehicle is selected edits the line, which matches the way order edits already go to the line.

Assigning a vehicle to a line leaves the line's blocks as they are. Private blocks are not copied onto the line. Taking the vehicle off the line copies the line's orders and the line's blocks onto the vehicle at that moment, the same moment the order copy already happens. From then on, Rules for that vehicle edit the private copy.

A line with vehicles of one kind refuses a check that only makes sense for another kind, in the same way it refuses a stop of another kind.

## Later features

Shunting, cargo demand, and passenger destinations add sentences and actions to this palette. They do not add a second rules window. A yard opens this same window on the yard's list. Until those features exist, their blocks are absent from the palette, and a saved block of that kind takes Else and shows unavailable in the footer.

## Save

Rules are not written into `S5::GameState`. The implemented file is `<save>.SV5.orules`, beside the lines and signals companions, version 1.

An optional `PHASES 1` extension to the v1 companion records block timing and resumable physical-approach/arrival state. Legacy companions without this extension still load with Automatic timing. If a legacy land wait was armed while its route-planning head was ahead of the physical front, loading restores the approach and starts a fresh timeout after reaching the point. Malformed phase data is rejected before attachment. Builds predating the extension reject the extra data; keep the checkpoint's saves separate from feature-branch disposable saves. The packed save and existing lines/signals versions remain unchanged.

A save with no rules file loads with no blocks. Orders and lines still load. Deleting a vehicle drops its private rules in that same command, so a reused vehicle id does not inherit them. Line rules are keyed by line id. Private rules are keyed by the vehicle head id, the same kind of id the lines companion already stores.

A running list with no blocks is unchanged.

## Tests

Before the window: a list with no blocks advances as it does now. Cargo below a number skips a group of two stops and one waypoint. Cargo at that number visits the group. A full vehicle goes to a named stop on the same list. Repeat 2 runs a group twice and then the following order, and the next visit starts at 2 again. Two vehicles on one line share the blocks and keep separate counts. An unavailable late check takes Else. A repeat above 20, a fifth nested C-shape, a Break without an enclosing loop, and a Go to on another list are refused. The 256-step budget pauses until the next tick. The same inputs produce the same next order.

With the window: the Rules button uses the empty vehicle tab slot and the empty line-window slot described above. Opening it avoids the line window, the vehicle window, and schemes when screen space permits, with compact sizing and normal overlap fallback. Dragging a block does not change the map tool. An in-game check is required for the drag and the placement. A unit test does not replace that check.

The companion round-trips a group on a line and a group on a private vehicle. A missing rules file leaves the orders in place.

## Implemented editor and runtime

The native Rules window opens from the vehicle tab gap or the line window's R button. An assigned vehicle identifies the shared line being edited. The palette stays visible while the list scrolls. Drag row bodies to select consecutive rows, then drag their left grip to move them. Drag a bracket header to move its whole section. A label follows the pointer with the block sentence or section being moved; a line marks the proposed insertion. Drop on the bin to delete. Escape, closure, and outside drops cancel without submitting an edit. Native scroll release handling is separate from map tools.

"This stop has cargo" and "This stop has no cargo" are simple presets for selected cargo quantity greater than zero and equal to zero. Cargo waiting also supports below/equal/above a numeric threshold. Select Stop and Cargo explicitly. Vehicle full checks every usable compartment, rather than comparing summed quantities. Wait's Value is the comparison threshold; Minutes is its separate timeout. Repeat uses Times. An unavailable station, cargo, platform, or timetable slot reports a reason and follows Else.

Else can wrap the same rows as an If containing Skip or Go to, or a later section. Branch decisions persist through that section; Repeat starts fresh on a new visit. Station waits keep cargo handling active. Wait timeout uses 40 simulation ticks per minute and continues with "time limit reached." A 256-step pause holds physical progression and resumes the decision next tick. Ordinary lists bypass the runner.

The model lives in `Rules/Rules.cpp`, game bindings and companions in `Rules/RulesGame.cpp`, platform queries in `Rules/Platforms.cpp`, geometry in `Rules/Editor.cpp`, and native widgets in `Ui/Windows/RulesWindow.cpp`. UI edits submit validated commands; row references use stable keys. Existing vanilla edits update those keys. Deleted Go to destinations disable the action; choosing a new destination repairs it. Structural changes reset pending execution at a safe boundary while preserving the reached order for timetable decisions.

Bounds are 256 block records, four enclosing control blocks, Repeat 1–20 or Forever, Wait 1-1440 minutes, and the existing 63-row order limit. Companion parsing additionally limits 2048 owners and 256 MiB. Platform identities use canonical connected-track anchors.

Rules v1 uses `<save>.SV5.orules` (`ORULES 1` header), after `.olines` load. It stores exact keyed order contents, owner identity, block definitions, and independent vehicle cursors, repeat counters, branch choices, waits, and pending evaluation. Missing companions clear rules. Corruption or identity/order mismatch warns without attaching incorrect definitions. Writes use a temporary companion and atomic replacement; failed writes fail the save operation. The packed Order and S5 state and existing companion versions remain unchanged. Keep all companions beside their matching save.

## Section resizing and program feedback (2026-10-12)

Select a bracket, then set First row/Last row or drag its small end grip to another row. This expands If, Else if, Else, Repeat, Wait and Group across multiple stops, waypoints and cargo instructions. Skip/Go to and a same-section Else follow the resized parent atomically. Crossing sections or invalid boundaries reject the complete edit. End remains outside the section. Existing consecutive-row selection remains available for creating a whole section in one drag.

Green means the runner's last recorded check passed; red means it failed or was unavailable. Wait stays red while its check is false and turns green on passing. These are read-only views of decisions, not new evaluations. An active marker and current-order arrow show where the selected vehicle is executing. Edits clear stale decisions. A shared-line view identifies the vehicle used for progress (the related route-tool vehicle, otherwise its first assigned vehicle); each vehicle still has independent counters and waits, visible in its own window.

Normal order tabs use the shared variable block layout, with 16-pixel order bodies, 14-pixel clickable fields and 18-pixel closing slots; Rules uses the same geometry with selection grips. Lists without blocks keep their original 10-pixel rows. The palette uses two columns in compact heights to keep all nine block kinds reachable without covering the editor controls. The line clock sits in free space beside Schemes. Full physical behavior and the new native interactions are for the user's manual acceptance; no additional automated game mouse input is required.

## Else chains and loops (2026-10-12)

The palette says Else and Else if. Else if links to its preceding If or Else if; Else terminates the chain. One branch can have only one following clause. The first passing branch wins, and later checks are not evaluated on that entry. Recorded choices remain green/red until fresh evaluation; repeated sections clear their choices on the next iteration. A clause wraps a later section, or the same section as a preceding branch with an action. Invalid links and crossing sections reject the whole command.

Forever repeats and Break use the same 256-step tick budget and physical order transitions as finite loops. Break clears the selected loop's counters and those of nested loops, skips its remaining orders on this pass, and continues after it. Removing a loop also removes Break actions that target it. A budget pause keeps the decision pending and prevents physical progression until it resumes.

The companion remains version 1. Existing kind value 2 is displayed as Else; Else if and Break append values 7 and 8. A Repeat definition and active counter of zero encode Forever. Finite loops still require a positive remaining count. Existing companions remain readable; builds predating these extra kinds cannot load a companion containing them. No packed Order, S5, lines or signals version changes.

## Adding items inside a block

Drag a stop or waypoint by its `=` grip into the block's `+ drop inside` slot. Each nested block has its own slot. The container grows to include the dropped orders, even when the dropped order was already next to its last row. Select consecutive rows, then drag their grip to insert several together. A grouped section moves with its blocks. Existing First/Last selectors and the selected block's end grip still resize sections.

To add another logic block, drag it from the palette onto an order inside the existing block; select several interior orders first to wrap a larger child section. Skip, Go to and conditional Break attach to the enclosing If/Else-if/Else branch. Drop Else or Else if on a preceding header or its closing slot to use the following order section; add following orders in Route/Lines first when none exist. Invalid positions show no insertion marker; invalid release gives feedback and leaves the list intact.

Explicit container drops grow enclosing sections and paired actions/Else chains in one validated command. They refresh the active map numbers and preserve the reached order. Ordinary insertions above/below a section remain outside it. Crossing sections, self-containment, invalid references, transport incompatibility and order-table capacity reject the whole edit.

## MakeCode visual reference

The [MakeCode editor](https://makecode.microbit.org/#editor) and [If block guide](https://makecode.microbit.org/blocks/logic/if) guide the shapes. Statements have rounded corners and top/bottom snap connectors; control blocks have a thick C spine, a visible inner cavity and a full closing arm. Boolean checks appear in hexagonal sockets and values in rounded white fields. Logic is blue, loops green, groups/orders purple and waits orange; recorded pass/fail colours override these on checked blocks. Adjacent Else/Else-if arms join their preceding branch. Palette items and pointer previews use the same silhouettes. All three order views use the shared native geometry, including reachable interior append slots and correctly indented grips. The native appearance awaits user manual acceptance.

## Direct field editing and compact controls

Rules opens at 560x320 and resizes down to 360x240 (D40). The palette stays visible on the left; numbered orders and compact fields scroll in the centre. **Details** is initially closed. Open it for First row/Last row, the alternate-check selector and the full set of section controls. Keeping it closed gives the canvas 58 extra vertical pixels. The deletion bin and both progress lines remain below the canvas; controls never share its rows.

Click a socket to edit it: check, named stop, cargo/platform, comparison, And/Or and destination use native dropdowns; Wait also exposes its timing dropdown; quantities, years, Repeat, Wait minutes and group names use native text dialogs. Both checks expose their own operand fields without toggling Extra. The `+` socket adds an And/Or check. Repeat accepts 1–20 or `Forever`/`0`; Wait accepts 1–1440 minutes. Group names use the existing 80-character limit. Dialogs initialize the full value even when its displayed field is clipped. Long menus use Previous page/Next page, keeping all choices reachable within native storage limits (D39/D41).

Clicking a field in an ordinary vehicle Route or shared-line Orders tab opens Rules on that exact block and field. Clicking the header focuses the block without opening a value dialog. Drag headers or the order `=` grip to move sections/orders; the field sockets edit instead of starting a drag. End and interior append slots retain their existing roles. A palette click creates a block on the consecutive selection, otherwise on the focused section, otherwise the first row. **Actions** offers Add Else if, Add Else and Remove block. Clauses require a valid following section (or a paired action scope); invalid or crossing definitions are rejected with footer feedback. Removing a block keeps its orders.

The UI prepares typed edit proposals and submits the existing validated commands. A callback is discarded when owner, block key or model revision no longer matches. Invalid text changes nothing. Accepting an unchanged value does not restart execution or waits; successful changed edits preserve the reached point and restart at the next safe boundary. Scroll position is retained and clamped. UI field layouts and dialog/menu state do not enter the save companion. Native visual and mouse acceptance remains user-run, with Phase 4 in TESTING.

Manual Skip records the newly selected stable row immediately, before another simulation tick. The white marker falls back to the vehicle cursor if a run has not yet selected a destination, skipping cargo rows and wrapping at End. Rules also uses this lookup for plain displayed lists. Existing reached/approach anchors retain priority during real waits and cargo handling. Detachment/deletion no longer leaves an otherwise valid marker absent (D43).

Shared-line Path/All draws a read-only current-choice projection through the same bounded interpreter. It evaluates from the beginning with fresh control state and current facts supplied by the selected progress vehicle; it stops at false waits or its 256-step/64-destination horizon. Inactive branch sections contribute no destinations or legs. Actual progress arrows and simulation branch choices remain separate (D46). There is no future cargo/occupancy forecast or forced route. Other vehicles may have different choices; All uses one representative per line. No-vehicle previews can use station/year facts, with vehicle checks unavailable. Rule facts are sampled periodically and missing geometric legs are built one per UI update, reusing a bounded endpoint cache. The line footer reports partial/wait/unavailable previews. See `SHARED_LINES.md` for cache and refresh bounds. Map rendering never advances physical simulation or alters per-vehicle counters/waits (D44).

Path/All condition changes retain the last completed map snapshot until its replacement is ready; Updating feedback distinguishes that temporary display from a completed current-choice preview. Build-cache eviction cannot remove published pieces, and All capacity limits stop repeated rebuilding. A completed empty Wait preview publishes once without changing other lines' lane positions. During station loading the map still uses fresh current conditions, while the physical white arrow stays at the reached station. The copied vehicle-progress projection remains available separately; Path/All never seeds committed travel or a moving origin (D46). Simulation branch evaluation and waits remain unchanged (D45).

D47 extends atomic display replacement to topology refreshes, including unrelated AI construction. Invalidating search geometry no longer blanks published paths. Hidden rule edits/skips do not touch visible geometry; line colour/order edits use view-only invalidation and reuse cached legs. Actual graph changes force replacement even for identical endpoints. Resetting Rules/Lines clears all snapshots immediately. This is transient display state; no rule execution or persistence semantics change.

D57 final verification (`91037492`, `codex/native-inline-rules`): reconfigured Debug/Release game builds and complete suites passed 447/447 each; focused Rules passed 182/182. The separate-window workflow is retired; the user controls the remaining compact native appearance/edit/drag acceptance. See development status and testing instructions for actual evidence and outstanding gameplay checks. Keep TESTING.

D59 (2026-10-14) records user acceptance of the delivered step and explicit approval to merge the complete logic branch into master. Phase 4 is COMPLETED (user accepted); this supersedes the earlier inline UI TESTING gate. Automated evidence remains 447/447 Debug/Release, focused Rules 182/182. Individual historical gameplay checks are retained as unitemized, not newly claimed as passed.
