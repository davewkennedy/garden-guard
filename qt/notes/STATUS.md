# Garden Guard++ — live status board

Maintained by claude2-f4 (tech lead). Read-only for everyone else.

## Current state (updated 17:06)
**Session A running.** Started 16:52:54, timebox 50 min, wrap-up from ~17:36, done by ~17:43.
All Session A features are integrated and passing. Remaining time goes to polish and Session B items (terrain, more mechanics) plus QA.

- Project: `~/projects/garden-plusplus` (copy of Garden Guard+ made 16:48; `~/projects/garden-plus` is the untouched rollback).
- Snapshots will be saved to `~/projects/snapshots/` at each milestone.

## Team and assignments
| Agent | Role | First task | Then |
|---|---|---|---|
| claude2-f4 | Lead | Engine refactor: per-map tile size and grid (others plan, don't edit, until it lands) | Focus fire, integration, test, summary |
| claude2-de | Maps and balance | Design 10 maps and 50 waves (shrinking grids, backtracks on 8-10, sky lanes) | Balance all 10 maps with the autopilot |
| claude2-63 | Bugs and plants | Wildcard bats | Grape Vine grounding, cactus as the scorpion counter |
| claude2-0c | Progression | Plant unlocks, combined "New bugs + plants" cards, relock, Continue | Teach the autopilot the vine and unlocks |
| claude2-3e | Visuals | Seasons visual themes, map select screen | Visual QA of all 10 maps |
| claude2-c7 | Messenger | Answers the user's status questions (no development). Reads these notes; anything else goes ONLY through claude2-f4, never to dev agents directly | |

## Milestones
- 16:52 Start. Snapshot `garden-plusplus-0-start.tar`.
- 16:56 **Engine refactor landed** (claude2-f4): each map has its own grid size (MapDef cols/rows). Tiles stay 64 logical px and the board scales to fit, so bigger grids look like smaller tiles. Build + all tests pass. Snapshot `garden-plusplus-1-refactor.tar`. All helpers released to edit.
- 17:02 Helpers stalled at the usage cap; all four nudged and resumed after the user added credits.
- 17:04 **Integrated build:** 10 maps (15x10 to 30x20, seasons, backtracks on 8-10), plant unlocks + combined cards + NEW badges + '-' key (claude2-0c), wild bats + Grape Vine + cactus scorpion-counter (claude2-63), season backgrounds + weather (claude2-3e), focus fire + multi-select + Upgrade all (claude2-f4). Build clean, simtest PASS, autopilot wins all 10 maps (7 lives). Snapshot `garden-plusplus-2-integrated.tar`.
- 17:05 Session B work pulled forward: claude2-63 on terrain (rocks, ponds, lily pads).
- Decisions: maps 4/5 swapped (4 = Harvest Hollow, 5 = Moonlit Garden, the first wild bats). Plant unlocks are stored on the plants (TowerDef unlockMap). The 11th plant's key is '-'.

- 17:08 claude2-3e: seasons (backgrounds + weather) and the "Choose map" screen landed. Now on big-map readability (sprite/text scaling).
- 17:10 Focus fire verified on screen (map 6). Title renamed to Garden Guard++ with its own save data.

## Backlog (next tasks as helpers free up)
1. Snow slows bugs on winter maps (a MapDef speed multiplier), and frost a bit weaker there (claude2-de)
2. Side panel height with 11 plants (shop 3 columns, or a scroll area)
3. A "vine" screenshot scene; QA of the vine and terrain on screen
4. README for Garden Guard++
5. Two entrances (a second ground path) on one map: bigger; only if time allows

## Problems / blockers
_(none)_
