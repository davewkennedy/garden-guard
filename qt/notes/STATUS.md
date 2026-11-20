# Garden Guard++ — live status board

Maintained by claude2-f4 (tech lead). Read-only for everyone else.

## Current state (updated 17:33)
**Session A COMPLETE.** Started 16:52:54; feature work ended 17:32; wrap-up 17:33. Final build: 0 warnings, simtest 86/86 PASS, autopilot wins Normal with 6 lives. Final snapshot `garden-plusplus-5-final.tar`. All helpers stood down. Test save data for Garden Guard++ was cleared, so the user starts fresh.

- Project: `~/projects/garden-plusplus` (copy of Garden Guard+ made 16:48; `~/projects/garden-plus` is the untouched rollback).
- Snapshots will be saved to `~/projects/snapshots/` at each milestone.

## Team and assignments
**Names (2026-11-20):** Hank = claude2-f4 (lead), Dale = claude2-de, Boomhauer = claude2-63, Bill = claude2-0c, Bobby = claude2-3e, Peggy = claude2-c7 (messenger). Old IDs remain in notes/<id>.md and build/<id> names.

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

- 17:12 claude2-0c: NEW badge moved to a corner ribbon (prices visible), window back to 860 px. claude2-3e: QA of all 10 maps clean; README rewritten for ++. claude2-63: terrain landed (rocks, ponds, plantable lily pads; frozen in winter).
- 17:12 Real-app smoke test (autopilot 3x, 60 s): no crashes, map 1 cleared.
- 17:13 Session B items in progress: two entrances (claude2-0c), wind gusts (claude2-63), Garden Journal 📖 (claude2-3e), winter snow + terrain placement + balance (claude2-de). Snapshot `garden-plusplus-3-polish.tar`.

- 17:14 Test F (focus fire) added and passing. claude2-de: winter snow (maps 8-10 slower bugs, weaker frost), terrain placed, balance: autopilot wins with 13 lives.
- 17:16 claude2-3e: 📖 Garden Journal (J). claude2-63: wind gusts (gust toast connected). 17:17 claude2-0c: two entrances (path2).
- 17:17 FINAL TASKS: claude2-de map 7 "Crossroads" + wind on 6-7 + final balance (17:32); claude2-63 late-map real-app smoke tests (17:32); claude2-0c map-mechanic intros on the card (17:30); claude2-3e vine scene + QA + showcase screenshots (17:32). Wrap-up by claude2-f4 from ~17:33.

- 17:19 claude2-de: map 7 Crossroads + wind on maps 6-7; autopilot wins with 9 lives (smooth curve). 17:19 claude2-0c: "🗺️ On this map:" mechanics on the card.
- 17:21 claude2-de: the autopilot uses focus fire (boss > golden > wild bat > healer). claude2-0c: README updated. claude2-3e: vine scene, QA, 10 showcase screenshots, wind streaks restyled.
- 17:22 claude2-0c crash sweep: 123 runs, 1 crash (mapclear10, a screenshot-only path) -> fixed by claude2-f4 with a guard.
- 17:23 claude2-de: notes/balance.md (front-loaded difficulty; waves 4-5 too easy). claude2-3e: WHATS-NEW.md.
- 17:24 claude2-63: real-app smoke tests on maps 5-10: all exit 0, mechanics verified in-app.
- 17:24 In progress: claude2-de making waves 4-5 the climax on maps 6-10 (keep or revert by 17:31). Then FINAL BUILD -> re-sweep (claude2-0c) + showcase refresh (claude2-3e) -> wrap-up.

- 17:25 claude2-de: waves 4-5 on maps 6-10 are now the climax (x1.3 swarms, x1.1 hp); autopilot still wins (6 lives).
- 17:26 **FINAL BUILD:** 0 warnings, simtest 82/82 PASS, autopilot wins all 10 maps (lives 20/20/20/18/18/16/14/14/12/won with 6). Snapshot `garden-plusplus-4-final.tar`. Final crash sweep (claude2-0c) + showcase refresh (claude2-3e) running; summary to the user follows.

## Backlog (next tasks as helpers free up)
1. Snow slows bugs on winter maps (a MapDef speed multiplier), and frost a bit weaker there (claude2-de)
2. Side panel height with 11 plants (shop 3 columns, or a scroll area)
3. A "vine" screenshot scene; QA of the vine and terrain on screen
4. README for Garden Guard++
5. Two entrances (a second ground path) on one map: bigger; only if time allows

## Problems / blockers
_(none)_
