# Garden Guard++ — live status board

Maintained by claude2-f4 (tech lead). Read-only for everyone else.

## Current state (updated 16:56)
**Session A running.** Started 16:52:54, timebox 50 min, wrap-up from ~17:36, done by ~17:43.
All 4 helpers are building their features in parallel. claude2-f4 is on focus fire.

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
- Decisions: maps 4/5 swapped (4 = Harvest Hollow, 5 = Moonlit Garden, the first wild bats). Plant unlocks are stored on the plants (TowerDef unlockMap). The 11th plant's key is '-'.

## Problems / blockers
_(none)_
