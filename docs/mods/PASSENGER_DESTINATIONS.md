# Passenger destinations

D68 accepts the implemented CargoDist portion and approves master integration. The historical proposal below retains future industry-market/population-generation work; completion does not implement those extensions. Tests and acceptance are recorded in development status.

## Current end-to-end behaviour (COMPLETED, D68)

Normal private/shared/optional-rule services support inner-town bus/tram stop → outer combined station → train/ship/aircraft → destination outer station → local feeder → final town stop, with reverse journeys where connections exist. Several final station destinations coexist in the same waiting pool and compartment. Intermediate acceptance does not end a journey addressed elsewhere. Different stops within one town remain distinct endpoints; native production and station catchment/acceptance are preserved. This uses station destinations, not individual people or building-level passenger agents.

D66 adds native cargo-adapter regressions for passengers and mail, private/shared train/ship/aircraft trunk services, both directions, local bus/tram feeders, accepting interchanges, same-town stops, partial quantities and mixed-origin reloads. It confirms the existing D65 routing rather than adding a separate passenger workflow. Use compact Cargo summaries; no destination window or transfer order is required. Recorded test results and D68 delivery acceptance are in development status.

Current delivery: native CargoDist is COMPLETED (user accepted D68), using automatic defaults in new and older saves and a `.ocargodist` companion. See [Cargo distribution](CARGO_DISTRIBUTION.md). This supersedes the classic-by-default and save-container gates below. Existing production and payment formulas remain; explicit industry-demand markets, wagon requests, and additional population-demand generation below are later proposals.

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

D65 automatic-transfer follow-up supersedes standalone CargoDist windows and congestion overlays. Use normal StopAt/private/shared/optional-rule routes and base-game combined StationIds. Inline Cargo-tab summaries and the per-game Options Misc Automatic/Classic preset replace the large UI; mixed modes and hidden settings persist. Reached-stop ownership/key/revision/signature context and UI caches are transient and reconstruct after load, preserving cargo when unresolved. Packed saves and companion versions remain unchanged. CargoDist is COMPLETED by D68 user acceptance; no consist/shunting or new save-container dependency is introduced.
