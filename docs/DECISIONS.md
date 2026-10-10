# Decisions

| ID | Date | Decision | Why | Consequence |
| --- | --- | --- | --- | --- |
| D1 | 2026-10-10 | Three layers: engine, `src/Modding`, packages under `mods/`. | Gameplay hardcoded in the exe cannot be installed separately. | Mods do not link into `OpenLoco.exe` as features. |
| D2 | 2026-10-10 | Do not implement NSF or NOF in this change. | Upstream has named both and has not published a spec or code. The v26.01 post says old formats stay supported. | Extended state waits. SV5 import stays exact. |
| D3 | 2026-10-10 | New saves will be a versioned chunk container, not a sidecar. | Sidecars split from the save and desync. | Container design must be able to wrap upstream NSF later. |
| D4 | 2026-10-10 | `.olmod` is a proposed zip with `manifest.json`. | No official mod package exists. | Revisit if upstream defines one. Directory loader ships first. |
| D5 | 2026-10-10 | No native DLL mods by default. | Untrusted code would have full process rights. | Scripts, when chosen, are sandboxed. |
| D6 | 2026-10-10 | Script language is not chosen. | Lua, Wasm, and a custom logic graph have different costs. See below. | Programmable logic starts as an in-engine instruction graph. A text form is a view of that graph. |
| D7 | 2026-10-10 | One logic runtime for lines, yards, and shunters. | Separate languages would drift. | Railway-only actions are capability-gated, not a second interpreter. |
| D8 | 2026-10-10 | API compatibility is same major.minor and running patch >= required patch. | `0.x` minor breaks would be hidden by a looser rule. | Bump minor when the manifest or loader behaviour breaks. |
| D9 | 2026-10-10 | Unknown manifest keys are ignored; nested objects are rejected. | Forward field adds should not break the loader. Arbitrary JSON should not become a hidden API. | Schema growth is explicit. |
| D10 | 2026-10-10 | Hybrid monorepo. | One repo keeps engine and mods in sync while the API moves quickly. Separate repos are better once packages are stable. | Gameplay mods may move out later. Examples stay here. |
| D11 | 2026-10-10 | Classic cargo and passengers stay the default. | The original game must remain playable. | Destination systems are mods or options. |
| D12 | 2026-10-10 | Shunting uses real movement and reservations. | Teleporting wagons would not match the request and would break signals. | Shunting cannot start until consists and reservations exist. |
| D13 | 2026-10-10 | Do not change map or object constants yet. | They are packed into the save. | `ENGINE_LIMITS.md` is descriptive. |
| D14 | 2026-10-10 | Do not activate every installed mod automatically. | A dropped folder must not change a game session. | Superseded in part by D15. The refusal to auto-run unlisted mods still stands. |
| D15 | 2026-10-10 | Activate only ids listed in `<mods>/enabled.json`. The `mods` command does this and exits. A normal launch does it for `%APPDATA%\OpenLoco\mods` only when that directory already exists. | Players need a path that loads a data mod without recompiling, and a missing folder must leave vanilla startup alone. | The first action is `log` only. No simulation mutation and no save write. |

## Script language tradeoffs

Not selected.

| Option | Advantage | Cost |
| --- | --- | --- |
| Lua | Small, known, easy to embed. | Must remove `io`, `os`, `package`, and `debug`. Determinism needs a banned or fixed math story. |
| WebAssembly | Stronger sandbox, language choice for authors. | Heavier toolchain, harder for players to edit, time and memory limits need a runtime. |
| Instruction graph plus text view | Matches the visual editor, easy to bound and save, deterministic. | Not a general programming language. External authors cannot reuse an ecosystem. |

Decision D6 stands until a milestone needs player-authored text. The first interpreter should execute the graph directly so the visual editor and the text view cannot diverge.

## Release artifacts

Engine builds stay the OpenLoco executable plus its open data. Mods ship as `.olmod` without original DAT, G1, or music files. OpenGraphics objects already fetched by CMake are a separate upstream asset pack and are not part of a mod.
