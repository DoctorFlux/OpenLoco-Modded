# Passenger destinations

Current delivery: native CargoDist is implemented in TESTING (D62/D63), using automatic defaults in new and older saves and a `.ocargodist` companion. See [Cargo distribution](CARGO_DISTRIBUTION.md). This supersedes the classic-by-default and save-container gates below. Existing production and payment formulas remain; explicit industry-demand markets, wagon requests, and additional population-demand generation below are later proposals.

## Earlier proposal (historical)


Not implemented. Classic passengers stay the default.

## Model

Do not simulate one agent per person. A group is a count of passengers with the same origin station, destination station, and trip leg. Groups merge when those keys match.

Generation uses town population and a demand weight between towns, drawn from the game PRNG. The player can scale the weight. Zero scale is classic mode: the current building production and anonymous cargo path.

## Journeys

A group picks a path over shared lines when lines exist. With no shared line, the group can only board a vehicle whose orders include the destination. If no path exists, the group waits and ages out through the existing cargo-age rules. A "passengers going to" check joins the Rules palette when this feature exists. Until then the palette does not show it.

Transfers are a group arriving at an intermediate station and becoming a new leg. Missed connections increase wait time. They do not spawn extra groups.

Travel time is the scheduled or observed leg time. Groups prefer the lower time, then the higher frequency. The choice is deterministic for the same inputs.

## Capacity

A vehicle loads a group only up to free passenger capacity. Partial groups are allowed. Revenue uses the existing payment function unless a destination-mode payment is explicitly enabled. Default destination mode still calls the current economy payment so income does not silently change formula. Any new fare rule is a separate, tested option.

## Modes

Rail, road, tram, ship, and aircraft participate when they can carry the passenger cargo type. The journey planner does not offer a mode the group cannot board.

## Save

Groups are mod state. Classic saves have none.

## Tests

Two towns, one direct service, one transfer. Classic mode produces the same station quantities as today for a fixed PRNG seed. That comparison is the compatibility test.
