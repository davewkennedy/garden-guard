# New plants (claude2-de)

Appended to the end of `data::TOWERS` (indices 7, 8, 9).

| id | name | emoji | mechanic |
|---|---|---|---|
| `rose` | Thorn Rose | 🌹 | `Projectile::Thorn`: a straight thorn flies to full range along the aim and damages each bug it passes, up to `pierce` bugs (3/4/6). Cost 95/95/150. |
| `corn` | Corn Popper | 🌽 | `Projectile::Kernel`: each volley fires a homing kernel at each of the `shots` leading bugs in range (3/4/5). Uses `fire()` + `stepHoming`. Cost 85/90/140. |
| `clover` | Lucky Clover | 🍀 | `Projectile::Aura`: doesn't attack. Plants within range (1.6/1.8/2.1 tiles) reload faster by `boost` (+20/30/45%). The strongest aura wins; auras don't stack. Cost 100/100/150. |

Extra: every max-level (level 3) plant now has three golden sparkles orbiting its pot (renderer only).

## Code
- data.h: `Projectile::{Thorn, Kernel, Aura}`; `TowerLevel::{pierce, shots, boost}`.
- game.h/.cpp: `Shot::{pierce, hitIds}`; `fireThorn`, `stepThorn`, `fireKernels`, `rateBoost` (applied in `updateTowers` to the cooldown tick). Aura towers are skipped like sunflowers.
- renderer.cpp: drawShots Thorn (pink thorn + petal trail) / Kernel (spinning popcorn); drawTowers clover ripple ring, no aim arrow on clover, level-3 sparkles.
- audio.cpp: `shoot_rose`, `shoot_corn` (the clover never shoots).
- mainwindow.cpp fillPanel: Pierce / Volley rows; the clover shows Range + Boost only.
- No new EffectKind values.

## Lead to wire up
- Shop keys/layout for plants 8-10 (rose, corn, clover).
- The clover has `damage = 0` and `rate = 0`, so anything that lists Damage/Fire rate in the shop tooltip should treat Aura like a sunflower.

## Status
Superseded by the balance pass below: simtest now PASSES.

## Balance pass (claude2-de, 15:06)
- simtest: Test 0 runs rose/corn/clover. Test 0b: a level-3 clover doubles a neighbouring sprout's wave-1 shots (10 -> 20). The campaign defence now includes rose, corn and clover.
- Map 4 Moonlit Garden: hpMult 2.2 -> 2.0, startCoins 280 -> 300 (path and night flag untouched).
- WAVES 4-10 now include Caterpillar, Cricket and Mosquito, plus one GoldenBeetle in w6 and w10. Grubs only come from splits. The old ant/beetle/wasp counts were trimmed a little.
- Campaign sim: lives after each map 20 / 20 / 10 / WON with 5. PASS.

## Round 2: campaign (claude2-de, 15:55)
- 5 maps x 5 waves, defined per map in data::MAPS (MapDef::waves); new map 5 Harvest Hollow (autumn, seed 1031).
- Bug unlocks: m1 Ant/Beetle/Wasp; m2 +Caterpillar, Butterfly; m3 +Cricket, Scorpion, Snail; m4 +Bat, Mosquito, GoldenBeetle; m5 everything (2 Snails in the last wave).
- Sky lanes: 0/1/1/2/2. Each crosses the path twice mid-map, then merges at the house.
- Map hpMult / startCoins (final, 15:58): 1.0/180, 0.7/320, 0.6/360, 0.5/500, 0.45/500. Wave hpMults carry the ramp.
- simtest: Test 0c checks waves, unlocks and lanes; Test 1 uses 2 chilis; Test 2 autoPlant also covers sky lanes.
- Autopilot think() now buys in a loop: it saves for the next planned plant early, upgrades with surplus over 150 coins, and spends everything going into a map's last wave.
- Autopilot lives after each map: 20/19/14/10/WON 6. Deterministic. Finale margin: hpMult 0.4 and 0.45 both give 6 lives, 0.5 gives 2.
- Follow-up: map hpMult < 1 should be folded into the wave multipliers; teach the autopilot frost at lane crossings so difficulty can go back up.
