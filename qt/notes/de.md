# claude2-de: maps, waves, simtest (Session A)

Status (16:58): **data landed.** 10 maps × 5 waves are in `data::MAPS`. All data checks pass (Test 0c, Test U). Balance is in progress: the autopilot currently loses at map 9.

## Maps

| # | Map | Grid | Season | Start coins | Sky lanes | Path notes |
|---|---|---|---|---|---|---|
| 1 | Sunny Meadow | 15×10 | Spring | 180 | 0 | unchanged |
| 2 | Apple Orchard | 16×11 | Spring | 320 | 1 | re-fit |
| 3 | Thorny Ridge | 17×11 | Summer | 360 | 1 | re-fit |
| 4 | Harvest Hollow | 18×12 | Summer (golden) | 500 | 2 | re-fit, moved from 5 |
| 5 | Moonlit Garden | 20×13 | Summer + **night** | 500 | 2 | one long right-to-left run |
| 6 | Amber Woods | 22×15 | Autumn | 560 | 2 | serpentine |
| 7 | Pumpkin Patch | 24×16 | Autumn | 620 | 2 | serpentine |
| 8 | Frost Fen | 25×17 | Winter | 680 | 2 | 1 short backtrack (5 tiles) |
| 9 | Snowdrift Hollow | 27×18 | Winter | 740 | 3 | 2 short backtracks (5, 3) |
| 10 | Evergreen Summit | 30×20 | Winter (finale) | 800 | 3 | 2 backtracks (8, 3), 3 snails |

The layout checker (`maps10.py` in my scratchpad) verifies every path is straight, stays in the grid, never overlaps or touches itself (at least 1 grass tile between passes), and runs from x = -1 to x = cols. It also checks that every lane runs from x = -1 to x = cols and crosses the path.

## Bug unlocks (Test 0c enforces them)

| Map | New bugs |
|---|---|
| 1 | Ant, Beetle, Wasp |
| 2 | + Caterpillar, Butterfly |
| 3 | + Cricket, Scorpion, Snail |
| 4 | + Mosquito, Golden Beetle |
| 5–10 | everything. Wild **Bats**: 2 / 1 / 2 / 1 / 2 / 2 per map (none on maps 1–4) |

Plant unlocks are claude2-0c's (`TowerDef::unlockMap`); Test U checks them.

## Waves

- Maps 1–5 use hand-tuned waves: the old 1–3, the old Harvest Hollow minus bats, and the old Moonlit with exactly 2 bats.
- Maps 6–10 come from a template. Swarm sizes grow ×1.25 → ×2.25, and gaps get tighter.

## simtest changes

- Mechanics tests (0, 0b, 1) set `g.unlockAll = true`.
- autoPlant skips locked plants.
- Test 0c covers 10 maps: grids, paths, unlocks, bats, lanes 0/1/1/2/2/2/2/2/3/3, and the finale's snails.
- Added claude2-0c's Test U.
