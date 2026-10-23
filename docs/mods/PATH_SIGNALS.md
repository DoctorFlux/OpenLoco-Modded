# Path signals, chain signals, and reservations

D77 changes only legend text: inherit the construction window secondary colour like native placement controls, removing forced black formatting. D76 arrow hues/layout and all signal safety decisions remain. Current verification is in development status.

## D76 consistent arrow colours and compact key (TESTING)

Supersedes the D75 directional colour table below: Block grey, Path yellow, Chain dark blue and Path+Chain orange in **both** directions. Each facing signal arrow independently uses its own type. The key has one sample per type in a two-row grid; the construction frame is 156x286 instead of 156x338, preserving all placement control positions and a separate cost footer. Arrow colours identify types; native aspects indicate clearance. No reservation, priority, tail-release or persistence change. Current verification and user-only visual acceptance are recorded in development status.

## D75 whole-train clearance and arrow key (TESTING)

A front/engine passing the exit never releases track still occupied by its rear wagons. The routing tail remains the reservation-release point. Native block recomputation additionally uses complete physical track footprints so a wagon on a remote part of a curve, or a detached/stationary wagon, keeps the conflicting block occupied. Ordinary handoffs inspect occupied protected holds even when no unused path claims remain. Occupied normal and standalone Chain posts show native red frame 0 throughout the wait; Path/combined retain the selected-path check so unrelated free paths remain usable.

The construction tab now includes the eight type/direction arrow colours in a two-way/one-way table. Block: muted green/grey; Path: white/yellow; Chain: blue/dark blue; Path+Chain: green/orange. These arrows identify signal type/direction, not permission to proceed. Rendering and the menu share one mapping. Current automated results, remaining user-only tail/legend acceptance and the unchanged persistence versions are in development status.

## D74 merge safety and persistent priority (TESTING)

Standalone **Chain is block-based**: it needs a clear native whole block and the required exit lookahead. It cannot admit a second train merely because a different path through that occupied block is free. **Path+Chain** combines path protection with chain exit lookahead. Ordinary signals retain native whole-block admission. Conflict scheduling also compares ordinary-signal requests; unrelated vehicle slots remain unchanged.

When planning commits the entrance, the complete reserved section becomes non-yieldable. Express and wait priority select among uncommitted requests; neither can steal an entered route. Invalidating pending execution retains the committed suffix. Replacing it requires successful validation of the complete replacement before releasing old unused pieces; occupied protection remains until the tail clears. The final physical entry guard checks every actual rail movement, including block-only games and stationary wagons.

Waiting age, exact-tie draws and departure holds survive matching save/reload in `.osignals` v4 together with committed claims. Attachment checks the native save bytes/tick and vehicle identity; older v1-v3 companions remain readable. Other companions and packed saves do not change. This supersedes D73's load-reset/block-only-scheduling statements and earlier v3-writer descriptions. Detailed wire/rollback notes are in `../SAVE_FORMAT.md`; actual tests and manual acceptance are in `../DEVELOPMENT_STATUS.md`.

## D73 reservation priority (TESTING)

Before vehicle updates, preview eligible rail requests using current orders and Rules choices. Only trains with overlapping directed-route footprints contend; unrelated vehicle slots stay in their existing order. Use this order of precedence:

1. The native **Express** vehicle setting precedes Local, including when the Local has waited longer.
2. Within the same class, an actual stopped signal waiter precedes a moving train with unused reserved lookahead.
3. Among waiting peers, prefer the longer continuous simulation-tick wait. Blocked station departures count even without a nearby display post.
4. For equal waits, prefer shorter remaining directed track cost to the next ordered stop/waypoint, or to the protected exit when it is earlier. Include front-piece progress.
5. For exact conflicts with equal keys, draw from the simulation PRNG in stable owner order. Cache the result through retries; drawing/querying the map never rolls a winner.

This permits a waiting train to take a distant equal-class train's **unused** lookahead. It does not revoke occupied track, committed routing-ring pieces, physical wagon protection or an unknown owner's claim. Express cannot override these checks. Revalidate the complete new path before yielding any claim, and retain all occupied holds until normal tail release. A displaced owner uses existing red signal feedback and normal safe planning/braking. Signal entry and station departure share this validation; station-through protection from D72 remains required.

Keep a stopped granted train's queue position until movement/commitment begins, avoiding alternating unused grants. Priority ranks belong to one current conflict component; a changed route cannot borrow a rank from an unrelated component. Existing pending paths provide only comparison footprints and target/cost metadata, avoiding repeated A* and physical-occupation scans for those paths every tick. Fresh requests and departures still search within 4096-expansion/64-piece bounds, use cheap footprint bounds for pair rejection and the tile index to locate conflicting pending owners. Reusing a footprint never grants movement or replaces the fresh safety check. This changes scheduling only where Path/Chain reservations are active; block-only gameplay retains native order.

Priority state is transient and rebuilt after pickup/reversal/load/reset. Signal waiting age and cached tie draws restart after loading; `.osignals` remains v3 and other saves/companions are unchanged. Automated results and the small user-only acceptance check are in development status.


D72 (2026-10-23), **TESTING**: station arrival is no longer a reservation endpoint; departure also requires safe protection before movement. This supersedes earlier platform-boundary, station-fallback and reservation-cap-as-endpoint statements in historical entries below. Current verification is in development status.


Implemented in the engine. Block signals stay the default, and a save with no companion file behaves as before.

## Signal types

The Signal tab keeps the normal signal object and the existing both-directions and single-direction buttons. Path and chain are checkboxes on that signal, so any combination is allowed. The flag byte is sent in the `createSignal` and `createSignalsAuto` commands (`ebp` bits 8–9), so repeat placement and the network command stream carry it.

- **Neither checkbox**: a block signal. One direction places one side; both directions place both sides.
- **Path**: uses the native clear/block post states. A train arriving at it reserves a path to the next safe waiting point. Its reservation must respect the current waypoint's track piece, direction and height. If the required route is unavailable, the train waits at red and retries every 16 ticks; an unrelated free branch is not a substitute.
- **Chain**: requires native whole-block clearance, then reserves the selected continuation while looking through chain signals, including a signal that has both checkboxes, and through the exit signal. Through a path exit it reserves to that signal's next safe waiting point. Through a block exit it reserves that whole block, and refuses an exit block that is already occupied. The train's required exit must lead to its ordered waypoint. A free exit that cannot reach that waypoint does not allow it to proceed. Posts use native block-signal states; a failed reservation displays red while the train waits and retries.
- **Both checkboxes**: a path signal that also looks ahead. Facing one starts a chain search. Native approach/passing aspects remain when the required route is available; the blocked-route override uses danger frame 0.

One direction is the single-direction button. A train that meets the back of a signal is blocked. Both directions places a signal facing each way, so each direction meets the front of its own signal.

A safe waiting point is a real facing signal boundary (with Chain lookahead through its exit) or a physical track end. Station/platform edges and waypoints are route targets, not safe endpoints. The 64-piece reservation cap and 4096-expansion search cap never authorize a partial unsafe route; exhaustion waits safely. Add signals to long unsignalled sections where necessary.

## Reservations

`src/OpenLoco/src/Signals/PathSignals.cpp` holds the reservation state. A reservation is separate from the 64-slot routing ring, so its length is not limited by the ring.

- A piece key is direction independent: the sequence 0 origin, rotation, and track id.
- Holds are either a pending path or occupied track. Every rail train holds the pieces in its routing ring. The head adds a piece when it writes the next routing, and the tail releases a piece when it leaves.
- Two pieces conflict when they share a tile at overlapping heights with overlapping quarter-tile clearance from `TrackData::getTrackPiece`. Switches and crossings conflict; parallel track does not.
- A reservation is all or nothing.
- A train in path mode protects the track it stands on and the path ahead. A train that has not passed a path or chain signal holds its track without protection, so vanilla trains keep vanilla rules among themselves.
- Reverse, lookahead collapse, pick-up, and placing a train rebuild its holds from the routing ring and drop its pending path. Selling a train releases everything.
- A hold whose train no longer exists or no longer lists the piece is dropped when another train meets it, and every 512 ticks.

## Pathfinding

`PathSearch` is an A* over an abstract track graph. The game adapter uses `getTrackConnections`, the signal types, the holds, and a vehicle scan of the piece's tiles.

- The cursor follows current and subsequent private/shared orders and a bounded read-only projection of actual Rules execution/current facts. Station membership comes from each actual track piece, not the junction-wide station ID. Waypoints retain exact piece/direction/height matching and the equivalent reverse traversal. Required station/waypoint destinations cannot be replaced by unrelated free branches. Beyond the protected boundary, only connectivity to the next destination is proved. No orders retain nearest-safe-point selection.
- Cost is `unkWeighting` per piece. The heuristic is the vanilla distance metric.
- Before the reservation end, held or occupied pieces are impassable. After it, they only add a penalty.
- The search stops after 4096 expansions. A waypoint that cannot be proved reachable produces no reservation: the train stays on the required order and waits, rather than taking an unrelated free dead end or exit. A genuine alternate route is allowed when it reaches that same waypoint. Required station orders now also require route proof; a train with no orders retains the nearest-free-safe-point behavior.
- Reachability can be proved beyond a real signal boundary without claiming track there. The 64-piece reservation horizon alone is not a safe boundary. Occupancy inside the reservation remains impassable; beyond its end it affects only look-ahead cost. The train can approach the next safe signal on the required route, then wait there for the remaining occupied section. This does not reserve through another train.

## Train integration

All decisions run in `sub_4ACEF1` before `setRouting`, so a refused move uses the existing stop at the end of the current piece. A path wait returns status 3 with flag bit 6. That wait never passes at danger, never takes the manual-control pass, and never reverses on a timeout.

- A train with a pending path follows it at junctions. Before adding another routing piece, it rechecks the remaining reserved prefix against physical occupation (including foreign wagons) and conflicting holds. A newly blocked prefix is released; already occupied holds and the required order remain, and the train waits red and retries after 16 ticks.
- A vanilla train will not enter a piece that a path-mode train holds, and will not pass a block signal into a block that holds another train's path.
- The player's pass-signal button cannot bypass the reservation check on a Path, Chain or combined signal, including junction arms. A free route to the required waypoint is still necessary. While special-signal rail logic is active, a pass request cannot authorize physical entry into another train's occupied or protected conflicting piece, including after an ordinary block-signal handoff. Free ordinary movement remains native; games without this logic retain the original behavior (D55).
- Native aspect writes still apply to path and chain heads; the waiting override is read-only. Every-eight-tick handling repairs older special-post aspects once after reset/load; it does not continuously repaint reservations. See Display.
- Shared-line trains reserve toward their current order, which already comes from the line.

## Display

Plain block signals keep OpenLoco's own look. OpenLoco's planning head runs ahead and writes native state 1 on the routed post, then states 2 and 3 on earlier posts. Native state 0 is the resting danger state, not green. Native aspect numbers depend on the object: a two-state signal proceeds at state 1; SIGC3 uses state 1/yellow and state 2/green; SIGC4 uses state 1/yellow, state 2/double yellow and state 3/green. Native frames are three times the state number. Tail departure, reverse and pickup restore state 0.

Path, chain, and combined signals use those same native writes when the required route is available. Reservations do not independently repaint posts. The one-time repair after reset/load restores the native resting danger state. While a train waits after a failed special-signal reservation, the renderer displays that head's red frame; this read-only override leaves packed occupancy, route lights and native animation state unchanged. It returns to the native frame when neither an actual wait nor a blocked approaching route applies. Multiple trains waiting on one head keep it red until the last wait clears, and the existing viewport invalidation callback updates it when that state changes. Ghost previews and ordinary block signals retain their native frames. The failed decision may be beyond the approach post at an unsignalled junction. The wait also marks the nearest facing special post in that train's routing ring, stopping at the first facing block signal and scanning at most 64 slots. Opposite-direction and earlier posts are not marked. Protected-piece and foreign-path refusals use the same notification. Successful reservation or actual native route progression clears only that train's marker; other waiting trains retain red (D48/D50).

Junction route indicators use vanilla `updateJunctionSignalLights`. They light when the head takes the junction after the signal. On a path signal that is right after it reserves. On special signals, the lit lights are cleared where vanilla state 0 would clear them: when the last wagon leaves the piece, when the tail is lifted, and when the train's lookahead is discarded or reversed. The housing bits stay.

The reservation stays on each piece until the tail, the last wagon, leaves that piece. The head entering a piece does not drop it.

`paintSignalSide` draws the game's one-way arrow sprite beside each signal only while the viewport's one-way arrow view is on. The arrow colour is the signal combination: block is muted green or grey, chain is blue or dark blue, path is white or yellow, and path plus chain is green or orange. The second colour in each pair is the one-direction signal.

That same view, and the train window path button, draw the reserved path as a light transparent copy of the rail sprite only, in the train's company colour, from the tail through the path ahead. Closing the train window turns the button off.

## Save

`<save>.SV5.osignals`, text, `OSIGNALS 3`: the signal flags and each train's path mode and pending path. Version 3 has the same layout as version 2. Builds that wrote version 2 could leave route-indicator lights on special signals, so loading a version 2 file clears the lights on every special side once (`Signals::onCompanionLoaded`). The next train through a junction lights the correct housing again. Version 1 files still load: block `0` is ignored, path `1` and the old one-way path `2` become the path flag, and chain `3` becomes the chain flag. One-way is which sides were built, so an old one-way path that was stored only as a kind is a path signal on the sides that exist. Track a train stands on is rebuilt from its routing ring after load. A save without the file loads with block signals only. The file is written and read from the path-based save and load in `src/OpenLoco/src/S5/S5.cpp`, next to the lines companion. Network stream saves do not carry it. The reservation viewer is not saved.

## Not implemented

OpenTTD entry, exit, and combo pre-signals as separate types. Path plus chain is the two checkboxes together. Road vehicles are unchanged. Platform choice is not forced.

## Tests

`PathSignalTests`: registry, side and piece round trip, crossing and parallel conflicts, all-or-nothing reservation, protected and unprotected holds, chain aspects, the claim post rule for path and chain signals, two-state proceed as green, each train's last claim, path and chain flags together, companion version 1 migration and the version 2 light repair, the reservation viewer piece set, companion version 3 round trip and bad files, and search cases for a junction with a blocked branch, the stop before the next signal, chain continuation through a path exit, path-plus-chain look-ahead, a chain with an occupied exit block, a chain with one open exit, a one-way signal from behind, and the no-orders fallback.

Directional-waypoint follow-up (2026-10-13, D48): implementation `a728d749` on `codex/directional-waypoint-signals`, based on block-logic status checkpoint `a7b98497`. Focused Debug signal suite passed 37/37 (33 abstract/model/display tests plus four native-map fixtures). Full Debug/Release suites each passed 377/377. New coverage verifies blocked required routes versus unrelated free exits, a genuine alternate route to the exact waypoint, bounded reachability beyond reservation length/signals, wrong-direction rejection, private/shared orders, physical wagon blocking and self exclusion, retry/resume on the unchanged order, red-frame selection and independent waiting owners. Native visual/movement acceptance remains user-run; status stays TESTING.

## Circular waypoint approaches and signalled junctions (D49)

A free circuit that returns to a previously visited directed junction before reaching the required waypoint is not a valid alternative. In the user's If/Else layout, an occupied outer waypoint 3 approach must not authorize the inner waypoint 2 circle just because the search could return around the network and reach waypoint 3 later. The train keeps waypoint 3, waits red before the blocked route, and retries that route when clear. Circular services still traverse successive ordered waypoints normally; genuinely non-circular alternatives remain available.

Junction identity includes entrance position, height and incoming connection direction, independently of which outgoing track piece was chosen. Search checks exact ancestry across Path/Chain/released boundaries, using a small bitset to avoid unnecessary ancestry walks; collisions cannot reject a valid route. Histories are kept separately so a cheap circular candidate cannot discard a valid alternate approach sharing its later endpoint. Existing 4096-expansion and reservation-length limits remain.

A facing path/chain/combined post on any junction exit now triggers reservation checks before vanilla branch selection even when the train has not entered path mode yet. The chosen start uses its own Path/Chain policy; an occupied block-signal start cannot be entered. Native passing colours and reverse-side occupancy are applied to selected special junction arms as on a single connection. Ordinary unsignalled junctions retain vanilla selection unless the train already uses path mode; the existing explicit pass override remains.

D54 supersedes the special-signal pass-override exception in D49: that override must also obtain a safe reservation, both on single connections and junction arms. Ordinary block-signal overrides remain native.

Implementation `b9f7eb33`, same `codex/directional-waypoint-signals` branch. Two loop reproducers failed on D48, including native track connections and a physical wagon on the waypoint. Focused Debug checks passed 45/45 (38 PathSignalTests, seven WaypointSignalFixture cases); full Debug/Release suites each passed 385/385. Final native screenshot-layout acceptance remains user-run.

## Governing approach post during a wait (D50)

Implementation `4f5faaae` follows the train's own directed routing pieces back to the nearest facing post when its failed decision is farther ahead. A special post there receives the red wait override; a facing ordinary block post ends the scan. The scan handles wraparound and reverse traversal within the existing 64-slot limit. Direct protected-piece and foreign-path waits use the same notification. Native progression clears that train's wait without clearing other owners. Packed animation/occupancy, junction route lights and save formats are unchanged.

Four generated-map regressions added; the yellow-approach reproducer failed on D49. Focused Debug signal checks passed 49/49; full Debug/Release suites passed 389/389 each. Original-object visual confirmation remains the user's manual check; status stays TESTING.

## Red during approach and waiting (D52)

Implementation `049ef968` on `codex/waypoint-signal-aspects` adds a simulation-side display probe before normal rail progression. From the physical train front it follows at most eight already planned pieces, then only unambiguous connections, to the next facing post. A facing ordinary block post ends this inspection; unresolved junction selection stays with the existing native reservation path. At a special post it checks the current private/shared directional waypoint with the same Path/Chain search, occupied wagons, self exclusion, target proof and non-circular alternatives used for reservations. An unavailable required route contributes a red aspect while the train approaches, without reserving track or starting a simulation wait. Station/no-order fallback colors remain native. Existing reservation horizons still apply; the probe does not make an unavailable section beyond a safe waiting point into an extra reservation.

Each train probes at most once every 16 simulation ticks, with the existing 4096-expansion search limit. An active reservation wait already supplies red and skips the extra search. Approaching and waiting owner sets are independent; the renderer checks their combined aspect, so advancing the planning cursor or clearing one wait cannot briefly restore yellow while another blocked approach/wait applies. Viewport invalidation occurs only when that combined red state changes. Successful later probes restore native color; deletion, pickup/rebuild and game/save clearing remove transient owner/probe records. No native animation, occupation bits, junction lamps, current orders or companion data are modified by the display probe. Ghosts keep native preview rendering.

Native-map coverage includes red before reaching the post, shared Chain/combined approach-to-wait continuity, clear/self-excluded routes, block boundaries/ghosts and the screenshot's small-circle layout. Model coverage checks 16-tick throttling, multiple approaching/waiting owners, redraw continuity, forgetting/reusing IDs and reset. Final visual acceptance remains user-run; status stays TESTING.

## Actual native danger frame and free junction arms (D53)

The previous waiting/approaching override incorrectly selected frame 3. Read-only inspection of the user's SIGC3 art with the game's palette established frame 0 as red, frame 3 as yellow and frame 6 as green; SIGC4 also uses frame 0/red, frame 3/yellow, frame 6/double yellow and frame 9/green. The six signal objects in the latest autosave span the supported 4/7/10-frame families. New native signals start at frame 0. D53 uses that resting danger frame for blocked Path, Chain and combined posts, and restores the exact native frame when the override clears. Earlier tests shared the incorrect frame-3 assumption; their numeric expectations have been corrected. This section supersedes earlier color-number descriptions in D48–D52.

Logical display colors are mapped explicitly to native aspect numbers: red is 0, yellow is 1 (proceed on two-state objects), and green is the last supported state. The one-time special-post repair uses native resting danger. Animation/occupation bits and junction lamp overlays remain independent; original art is not bundled or changed.

For a signal on an unplanned junction arm, the approach probe now checks all outgoing starts in one bounded waypoint search, applying each start's own signal kind. A free branch that cannot reach the required directional waypoint cannot clear that train's signal. A genuine route to the same waypoint remains valid. The existing eight-piece inspection, 16-tick throttle and single 4096-expansion search limit remain. No movement, order progression or companion format changes.


## Occupied-route admission correction (D54)

The user reported actual movement into occupied track on D53. Native regression tests reproduced three unsafe admission paths: pending reservation reuse after a foreign wagon entered the reserved prefix, and pass-counter bypasses on single special-signal connections and signalled junctions. D54 (`023ec618`, following `1a541252` and `ab57d310`) rechecks pending physical occupation/conflicts before routing writes and requires a safe reservation even during a special-signal pass request. A refused prefix releases pending claims, keeps occupied holds/path mode/orders, marks the governing post red and retries after 16 ticks. Ordinary block-signal overrides keep their native behavior; genuine alternatives to the same waypoint and existing safe reservation horizons remain.

Twelve new tests check actual route admission and physical-front refusal, all three kinds, private/shared orders, resumption, self exclusion and preservation of occupied holds. Three further regressions failed on `1a541252`: after the planner had already committed the signal piece, placing a foreign wagon there let the actual physical front enter it. `ab57d310` adds a physical-entry check before changing the front's track piece/routing handle. It also protects already committed pieces after the planner has passed a downstream ordinary block post, while an unrelated ordinary train retains native movement. Focused Debug signal/placement suite passed 73/73. Full configuration results are recorded in development status. The exact trigger in the player's active session and final movement/sprite acceptance remain user-run; status stays TESTING. No game controls, player-save writes, original art changes or companion version changes.

D54 placement isolation (`023ec618`): native temporary placement motion uses the existing non-simulation flag and its own validation; it must not add physical reservation waits. A regression first failed on `ab57d310`, then passed with the guard restricted to the live Vehicle2 simulation path. No additional saved or UI state is introduced.

D54 final suites passed 413/413 in each of Debug and Release after reconfiguration for `023ec618`; 73 focused signal/placement checks passed, including 69 signal tests. All five executable paths per configuration were confirmed. Evidence and the outstanding manual safety acceptance are recorded in development status.


## Full physical occupation and handoff safety (D55)

D54 failed the user's native recheck on `023ec618`. D55 (`99ee01af`) fixes local-tile fallback gaps inherited from master: a wagon elsewhere on a conflicting multi-tile curve still occupies that complete piece, and native movement endpoints can lie just outside element tiles. Physical queries discover directly conflicting pieces through local real track elements and scan a fixed immutable cache of their complete element/movement footprints in both directions. Candidate pieces/tiles are deduplicated; no global train scan or recursive expansion is added. Self vehicles, ghosts, unrelated tracks and separate heights remain clear.

Live physical-entry checks remain active after an ordinary block post clears path mode/protection, so that handoff cannot make a foreign train disappear from the safety check. Protected reservations still block; unprotected planning claims alone remain native. Placement motion stays separate. Waypoint proof and valid alternatives retain their behavior; a free unrelated branch cannot authorize the occupied required route.

Five new tests include three initial failures: remote conflicting-curve occupation, every sample of all 352 native rail directions, and the real ordinary-signal handoff. Coverage also checks reverse curves, private/shared Path/Chain/combined native admission, red/refused physical entry, resumption and self/ghost/separated-track exclusions. Focused Debug checks passed 78/78 including four placement tests. Final full configuration results are recorded in development status; the user's manual acceptance remains pending.

D55 final verification: Debug/Release builds and full serial suites each passed 418/418 with zero failures after reconfiguration for `99ee01af`. Five configuration-specific executable paths and 74 signal tests were verified in discovery JSONs; focused signal/placement checks passed 78/78. Logs and the remaining user-run native acceptance are recorded in development status. No compiler/linker warnings or game control; configure retained CMP0135.

## Waypoints inside occupied sections (D56)

The user reported continued unsafe admission on D55 (`99ee01af`), confirmed by `openloco_2026-10-13_11_39_48.log`. Two new native reproducers demonstrate the missing boundary: a free ordered waypoint lies before a stopped wagon or another train's pending reservation, with no intervening safe signal. The previous search finished successfully at that waypoint and never inspected the blocked continuation. This is a reproduced defect; the saved instant does not establish every trigger in the user's running layout.

D56 (`cf47b6db`) treats a route-through waypoint as direction proof, not a safe stopping boundary. Path/Chain/combined and extension searches continue checking physical occupation and foreign reservations through the existing safe waiting boundary. Chain exit-block occupation still applies after the waypoint. Occupation beyond a later safe signal does not prevent a free approach to that signal; proof of a farther waypoint still leaves that bounded reservation unchanged. Station-platform stopping remains native. Maximum work stays 4096 expansions and 64 reserved pieces.

When the native planner advances the waypoint order, it releases unused pending continuation claims and evaluates the next order afresh at its next decision, retaining already occupied/protected holds. This prevents a safety proof beyond one waypoint from committing later junctions against the following order. Refusal retains the waypoint and uses the same actual danger-frame-0 approach/wait display and retry. Physical entry protection, placement isolation and packed/companion versions remain unchanged. Signals stay TESTING pending the user's manual acceptance; no automated game control is performed.

D56 validation: two native reproductions failed on D55, then nine added regressions passed with the fix. Focused Debug checks passed 87/87 (46 model/display/search, 37 native-map, four placement). Full Debug/Release suites each passed 427/427 after reconfiguration and both game builds; all five configuration-specific test executable paths were checked. Evidence: `build/windows/waypoint-boundary-{debug,release}-{build,tests}.log`, focused/before logs and discovery JSONs. No compiler/linker warnings; native acceptance remains user-run.

## Station departure safety (D72)

An incoming train reserves through its stopping station to a real safe boundary. If the required exit is occupied/reserved before that boundary, the entrance stays red. A station with a genuine departure signal can be entered with protection up to that post; Chain still requires its usual continuation.

Before rail departure from loading, timetable/rule holds or a stopped restart, a local unsignalled-section walk checks whether Path/Chain protection applies. It stops at boundary posts, so unrelated special signals do not change block-only stations. The departure proof covers the physical front through already committed routing to a safe continuation. Physical stationary trains, wagon overhangs and conflicting foreign holds prevent departure; self and separate parallel track remain clear. Both movement components remain held and retry after 16 ticks. Native cargo handling continues. Reversal rebuilds protection for the new front before departure.

Successful replacements validate all pieces before releasing unused old claims. Occupied holds survive failures and release as the tail leaves. Current order/progress and Rules runtime remain intact; projections use current facts without predicting post-loading cargo. Real departure evaluates the resolved outgoing orders again. Transient waits are cleared with owner/world lifecycle and rebuilt after loading; `.osignals` stays version 3.

Final D72 automated checks: Debug/Release **603/603** each; focused signals **103/103**. The station cursor projects through stops to its next waypoint; passing that waypoint replans the next leg while retaining occupied protection. A terminal station can request reversal after reverse-route proof, and the new front is gated again. Verification uses an unchanged source-hash manifest; see development status for actual times, warnings and the outstanding user-only manual gate.
