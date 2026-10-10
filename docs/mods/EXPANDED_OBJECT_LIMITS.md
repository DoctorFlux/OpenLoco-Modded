# Expanded object limits

Not implemented. Do not raise the constants in `Limits.h` until saves can store the new sizes.

## Audit

Current caps are listed in `ENGINE_LIMITS.md`. The hard ones for authors are vehicle objects (224), total objects (859), cargo types (32), track types (8), and signal types (16). `ObjectHeader` is 16 bytes and is packed into the save.

DAT files remain the original object format. Upstream NOF is intended to remove DAT limits and stay human readable, and it is not designed yet. This project loads original DAT files for as long as the engine does.

## Direction

- Keep DAT import.
- Add objects beyond the old caps only in the new save and object container.
- Allocate object tables dynamically with an explicit maximum that is a memory budget, not a claim of infinity.
- Identifiers that are too narrow get a wider id in the new container. The vanilla id remains for imported saves.
- The scenario object picker gains search, categories, filters, sort, favourites, presets, dependency display, and a missing-object report. That UI can be built against the current list before the caps move.

## Tests

Load a vanilla scenario's object list unchanged. Load a fixture with more than 224 vehicle objects only after the container exists, and show a clear error on the old format instead of truncating silently.
