# Larger worlds

Not implemented. The playable map is 384 by 384 tiles. `kMapPitch` is 512. Do not change those constants in isolation.

## Candidates

512, 1024, and 2048 tiles on a side are targets to measure, not supported sizes. A size is acceptable only when generation, pathfinding, rendering, the minimap, stations, industries, AI, and save time stay within a budget recorded in the test notes.

## Work required

- Coordinate types wide enough for `tiles * 32`.
- Tile storage that is not a single flat array sized by a compile-time pitch, or a pitch chosen per map and stored in the save.
- Entity spatial lookup resized with the map.
- Map window and minimap sampling, not a full-resolution texture of a 2048 map.
- Station catchment that is not a dense full-map grid if that grid becomes too large.
- Generator settings: town spacing, town clusters, industry density, rural gaps, terrain, and heightmap import.
- AI that does not pathfind the whole map every tick.

## Visuals

Tile size and the existing sprite style stay. The world gets larger by having more tiles, not by zooming the art out.

## Tests

`TileManagerTests` must be updated in the same change as any size change. Add a small generated map and one map above 384. Importing a 384 SV5 still works.

## Performance

Profile before changing algorithms. Land pathfinding is called out upstream as likely blocked on NSF. Follow that warning.
