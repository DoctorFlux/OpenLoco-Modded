# Path signals, chain signals, and reservations

Implemented in the engine. Block signals stay the default, and a save with no companion file behaves as before.

## Signal types

The Signal tab keeps the normal signal object and the existing both-directions and single-direction buttons. Path and chain are checkboxes on that signal, so any combination is allowed. The flag byte is sent in the `createSignal` and `createSignalsAuto` commands (`ebp` bits 8–9), so repeat placement and the network command stream carry it.

- **Neither checkbox**: a block signal. One direction places one side; both directions place both sides.
- **Path**: uses the native clear/block post states. A train arriving at it reserves a path to the next safe waiting point. Its reservation must respect the current waypoint's track piece, direction and height. If the required route is unavailable, the train waits at red and retries every 16 ticks; an unrelated free branch is not a substitute.
- **Chain**: uses the same reservation, but keeps looking through chain signals, including a signal that has both checkboxes, and through the exit signal. Through a path exit it reserves to that signal's next safe waiting point. Through a block exit it reserves that whole block, and refuses an exit block that is already occupied. The train's required exit must lead to its ordered waypoint. A free exit that cannot reach that waypoint does not allow it to proceed. Posts use native block-signal states; a failed reservation displays red while the train waits and retries.
- **Both checkboxes**: a path signal that also looks ahead. Facing one starts a chain search, and the path flag still turns the signal clear while it is reserved and red after the train passes.

One direction is the single-direction button. A train that meets the back of a signal is blocked. Both directions places a signal facing each way, so each direction meets the front of its own signal.

A safe waiting point is the next path, chain, or block signal facing the train, the end of the destination platform, a dead end, or 64 pieces of unsignalled track.

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

- The target is the current order: any piece reporting the order's station, or the exact encoded waypoint piece/direction and height (including the existing equivalent reverse traversal). A same-position piece with another orientation does not satisfy it. Waypoint orders require explicit reachability; station orders retain their existing fallback policy (D48).
- Cost is `unkWeighting` per piece. The heuristic is the vanilla distance metric.
- Before the reservation end, held or occupied pieces are impassable. After it, they only add a penalty.
- The search stops after 4096 expansions. A waypoint that cannot be proved reachable produces no reservation: the train stays on the required order and waits, rather than taking an unrelated free dead end or exit. A genuine alternate route is allowed when it reaches that same waypoint. Station orders retain the closest-free-safe-point fallback; a train with no orders retains the nearest-free-safe-point behavior.
- Reachability can be proved beyond the next signal or 64-piece reservation horizon without claiming track there. Occupancy inside the reservation remains impassable; beyond its end it affects only look-ahead cost. The train can approach the next safe signal on the required route, then wait there for the remaining occupied section. This does not reserve through another train.

## Train integration

All decisions run in `sub_4ACEF1` before `setRouting`, so a refused move uses the existing stop at the end of the current piece. A path wait returns status 3 with flag bit 6. That wait never passes at danger, never takes the manual-control pass, and never reverses on a timeout.

- A train with a pending path follows it at junctions.
- A vanilla train will not enter a piece that a path-mode train holds, and will not pass a block signal into a block that holds another train's path.
- The player's pass-signal button still lets a train past a path signal, but never into a piece another train has reserved.
- Vanilla aspect writes skip path and chain heads. Their posts are recomputed every 8 ticks from claims; see Display.
- Shared-line trains reserve toward their current order, which already comes from the line.

## Display

Plain block signals keep OpenLoco's own look. OpenLoco's train head runs ahead of the train and sets the route. A block signal goes to state 1 (red) when the head routes into its piece, and back to 0 (green) when the last wagon leaves it, the train reverses, or it is lifted.

Path, chain, and combined signals use those same vanilla writes. They stay green while the line is clear. The head passing one sets it red, and sets the signals behind it yellow and then the next vanilla state, including path and chain posts. The last wagon leaving, a reverse, or a lift sets the post back to green. Reservations do not repaint posts. While a train waits after a failed special-signal reservation, the renderer displays that head's red frame; this read-only override leaves packed occupancy, route lights and native animation state unchanged. It returns to the native frame when the wait clears. Multiple trains waiting on one head keep it red until the last wait clears, and the existing viewport invalidation callback updates it when that state changes. Ghost previews and ordinary block signals retain their native frames. A train waiting for a path does not turn the post yellow (D48).

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

Implementation `b9f7eb33`, same `codex/directional-waypoint-signals` branch. Two loop reproducers failed on D48, including native track connections and a physical wagon on the waypoint. Focused Debug checks passed 45/45 (38 PathSignalTests, seven WaypointSignalFixture cases); full Debug/Release suites each passed 385/385. Final native screenshot-layout acceptance remains user-run.
