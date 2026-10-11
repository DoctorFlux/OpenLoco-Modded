# Agent instructions

This fork is the OpenLoco Advanced Modding Framework. The baseline is upstream OpenLoco `e71abfb3` (project version 26.09) on branch `dev/local-baseline`. `origin` is `https://github.com/DoctorFlux/OpenLoco-Modded.git`. `upstream` is `https://github.com/OpenLoco/OpenLoco.git`.

## Read first

1. This file.
2. `docs/MASTER_PLAN.md`.
3. `docs/DEVELOPMENT_STATUS.md`.
4. The subsystem document for the task (`docs/` and `docs/mods/`).
5. `docs/DEPENDENCIES.md` and `docs/DECISIONS.md`.

`docs/ROADMAP.md` is the implementation plan. `DEVELOPMENT-ROADMAP.md` is only the map of the current vanilla source. If they disagree, `docs/` wins.

## Working rules

- Inspect the code before naming an API, plugin system, or save format. Do not invent one that already exists, and do not pretend one exists when it does not.
- Keep original Locomotion gameplay and presentation working. New behaviour is opt-in.
- Put shared lines, timetables, and path and chain signals in the game. Do not add a mod loader.
- Game-state changes go through validated commands. Do not mutate simulation state from UI or script code.
- Do not load untrusted native DLLs. Scripts, when a language is chosen, get no filesystem, process, or network access.
- Do not bundle original Locomotion assets. Do not modify `D:\SteamLibrary\steamapps\common\Locomotion` or player saves in `%APPDATA%\OpenLoco`.
- SV5/SC5 saves stay importable. Never overwrite an original save during conversion.
- Upstream NSF and NOF are not implemented yet. Do not publish a competing format. Read `docs/UPSTREAM_TRACKING.md` before save or object work.
- Do not mark a feature `COMPLETED` without a test or an in-game check recorded in `docs/DEVELOPMENT_STATUS.md`.
- Do not force-push or rewrite unrelated history. Use a feature branch for each significant change.
- Local Windows build and launch steps are in `README-LOCAL-SETUP.md`.

## After every task

Update `docs/DEVELOPMENT_STATUS.md`, `docs/DECISIONS.md` when a decision changes, and `docs/ROADMAP.md` or `docs/FEATURE_MATRIX.md` when scope moves. Record files, tests, blockers, and the next concrete task.
