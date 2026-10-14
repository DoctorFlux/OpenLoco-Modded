# Cargo demand

Current delivery: native CargoDist is implemented in TESTING (D62/D63), using automatic defaults in new and older saves and a `.ocargodist` companion. See [Cargo distribution](CARGO_DISTRIBUTION.md). This supersedes the classic-by-default and save-container gates below. Existing production and payment formulas remain; explicit industry-demand markets, wagon requests, and additional population-demand generation below are later proposals.

## Earlier proposal (historical)


Not implemented. Classic freight stays the default.

## Model

Industries and towns keep their current production. Destination mode adds a demand record: cargo type, consumer, quantity wanted, priority. Produced cargo is tagged with a destination when a matching demand exists. Untagged cargo uses the classic anonymous pile.

`StationCargoStats` already stores an origin station and an industry id. It does not store a destination. Destination mode must not widen that struct until the save container exists. Until then, destination tags live only in mod state and tests.

## Requests

A freight line may request a wagon type and count for a destination. If shunting is installed, the request becomes a yard job. If shunting is absent, the request is stored and the line loads whatever matching cargo is already waiting. The line does not stall forever: a timeout departs with a partial load.

After delivery, unwanted wagons can be released only when shunting is installed. Otherwise they stay on the train.

## Routing

Cargo prefers a line that stops at the destination. Interchange stations split a pile into onward groups the same way passenger groups split, but by cargo type and destination, not by person.

Partial loads are normal. Waiting cargo ages with the existing age field.

## Dispatch

A rules check can read cargo waiting. Freight destination becomes a check in that same palette when this feature exists. The order list and its blocks remain the dispatch. There is no second scheduler.

## Tests

Classic mode matches current delivery for a fixed seed. Destination mode delivers to the demanded industry rather than the first accepting station. A request with no shunting mod still lets the train depart.

D65 automatic-transfer follow-up supersedes standalone CargoDist windows and congestion overlays. Use normal StopAt/private/shared/optional-rule routes and base-game combined StationIds. Inline Cargo-tab summaries and the per-game Options Misc Automatic/Classic preset replace the large UI; mixed modes and hidden settings persist. Reached-stop ownership/key/revision/signature context and UI caches are transient and reconstruct after load, preserving cargo when unresolved. Packed saves and companion versions remain unchanged. CargoDist remains TESTING pending a short user coal/passenger/save check; no consist/shunting or new save-container dependency is introduced.
