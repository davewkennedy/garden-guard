# claude2-de: maps, waves, simtest (Session A)

**Final status (17:18):** full build clean. simtest PASS (82 checks, 0 failures; two runs gave identical output). The autopilot wins all 10 maps with **9 lives**.

## Maps

| # | Map | Grid | Season | Start coins | Lanes | Specials | Autopilot lives after it |
|---|---|---|---|---|---|---|---|
| 1 | Sunny Meadow | 15×10 | Spring | 180 | 0 | | 20 |
| 2 | Apple Orchard | 16×11 | Spring | 360 | 1 | | 20 |
| 3 | Thorny Ridge | 17×11 | Summer | 420 | 1 | rocks | 20 |
| 4 | Harvest Hollow | 18×12 | Summer | 500 | 2 | pond + lily pads | 18 |
| 5 | Moonlit Garden | 20×13 | Summer, **night** | 500 | 2 | pond; first wild bats | 18 |
| 6 | Amber Woods | 22×15 | Autumn | 600 | 2 | boulders; **wind** every 12 s | 16 |
| 7 | Pumpkin Patch | 24×16 | Autumn | 660 | 2 | **Crossroads** (second entrance from the top, crosses the path 3×); **wind** every 12 s | 14 |
| 8 | Frost Fen | 25×17 | Winter | 740 | 2 | frozen pond; **snow**; backtrack | 14 |
| 9 | Snowdrift Hollow | 27×18 | Winter | 1100 | 3 | pond + rocks; **snow**; 2 backtracks | 12 |
| 10 | Evergreen Summit | 30×20 | Winter (finale) | 960 | 3 | rocks + pond; **snow**; 3 snails | won, 9 |

- Snow: ground bugs move at ×0.85 and frost slows at ×0.8. Flyers aren't slowed.
- All map hpMult values are 1.0. Difficulty lives in the wave multipliers. The generator applies a per-map scale: 1.0, 0.9, 0.95, 1.15, 1.15, 1.15, 1.2, 1.0, 0.5, 1.15.

## Unlocks

| Map | New bugs | New plants (claude2-0c) |
|---|---|---|
| 1 | Ant, Beetle, Wasp | Sprout, Frost, Mortar, Sunflower |
| 2 | Caterpillar, Butterfly | Corn, Grape Vine |
| 3 | Cricket, Scorpion, Snail | Mushroom, Cactus |
| 4 | Mosquito, Golden Beetle | Chili, Rose, Clover |
| 5–10 | wild Bats, 1–2 per map (2/1/2/1/2/2) | none |

## simtest

- **0c**: 10 maps, grid sizes, paths from x = -1 to the house, unlocked bugs only, every map shows its new bugs, bat counts, lane counts, finale snails.
- **S**: snow pace is measured, 81.6 px/s against an expected 81.6.
- **P**: both entrances get used (15 / 15 bugs).
- **U**: plant unlocks (claude2-0c's draft).
- Tests 0, 0b and 1 use `unlockAll`; autoPlant skips locked plants.
- Test F (focus fire) belongs to the lead.

## Balance notes

- Difficulty has cliffs. Map 2 at ×1.15 loses outright, and maps 6–8 a notch harder drop the end result to 3 lives.
- Map 9 used to be a spike, losing lives in its early waves. Extra start coins (1100) fixed it.
- The finale's 3 lost lives barely depend on HP (×0.95–1.25 all give the same result). They likely come from wild bats or early waves.

## Tools (in my scratchpad)

- `maps10.py`: layout checker.
- `gen10.py`: writes MAPS, including terrain, snow, path2 and wind.
- `tune.sh`: balance sweeps.
