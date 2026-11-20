# Garden Guard+ 🌱

A cozy tower-defense game (C++20 / Qt 6). Bugs march along the garden path, or fly along sky lanes, toward your house. You plant defenders to stop them. Clear all 5 waves on each of the 5 maps to win the campaign.

## How to play

- Pick a plant from the shop (click it or press its key), then click a grass tile to plant it. Plants can't go on the path.
- Click a planted plant to see its stats, upgrade it (3 levels), sell it for 70% of what you've spent on it, or set a fire point.
- Squashed bugs pay coins. Clearing wave *n* pays a bonus of 20 + 5·*n* coins.
- A bug that reaches the house costs lives (the snail costs 5). You have **20 lives for the whole campaign**: they carry over from map to map, while coins and plants reset.

## Controls

| Key | Action |
|---|---|
| `1`–`9`, `0` | Select a plant from the shop (press again to cancel) |
| Click | Place the selected plant / select a planted one |
| `T` or 🎯 Set fire point | With a plant selected, click a spot inside its range (see below). Press again to clear |
| `N` | Start the next wave |
| `A` or 🤖 | **Autopilot**: the AI plays the whole campaign at 2× while you watch (press again to take over) |
| `F` | Cycle game speed 1x → 2x → 3x |
| `Space` | Pause / resume |
| `Esc` | Cancel placing or setting a fire point, or deselect |

## Campaign

5 maps × 5 waves. New bugs unlock map by map. The first time a bug appears, a **"New bugs!"** card introduces it.

| # | Map | Bug health (wave 1-5) | Start coins | New bugs | Sky lanes |
|---|---|---|---|---|---|
| 1 | Sunny Meadow | x1.00-2.00 | 180 | 🐜 Ant, 🐞 Beetle, 🐝 Wasp | none |
| 2 | Apple Orchard | x0.84-1.68 | 320 | 🐛 Caterpillar, 🦋 Butterfly | 1 |
| 3 | Thorny Ridge | x0.84-1.68 | 360 | 🦗 Cricket, 🦂 Scorpion, 🐌 Snail (boss) | 1 |
| 4 | Moonlit Garden | x0.80-1.55 | 500 | 🦇 Bat, 🦟 Mosquito, ✨🐞 Golden Beetle (**night map**: darkness, fireflies) | 2 |
| 5 | Harvest Hollow | x0.86-1.40 | 500 | none: the finale mixes every bug, ending with two snails | 2 |

Bug health is each bug's base HP times the wave's multiplier: it climbs from wave 1 to wave 5 on every map. **Sky lanes** are the dotted trails with chevrons that flyers follow instead of the path. Plan your anti-air around them.

Difficulty is tuned so the built-in autopilot (a decent but simple player) wins the campaign with normal coins; `garden_simtest` checks this.

## Plants

Prices are planting cost → upgrade to level 2 → upgrade to level 3.

| Key | Plant | Role | Air | Cost |
|---|---|---|---|---|
| 1 | 🌱 Pea Sprout | Cheap, fast single shots | ✓ | 50 / 60 / 100 |
| 2 | 🌿 Frost Fern | Chills bugs to slow them (40–60%) | ✓ | 70 / 70 / 110 |
| 3 | 🎃 Pumpkin Mortar | Lobbed shells that splash groups | ✗ | 100 / 100 / 160 |
| 4 | 🌵 Cactus Sniper | Long range, heavy hits | ✓ | 125 / 120 / 180 |
| 5 | 🍄 Spore Mushroom | Poison over time that ignores armor | ✓ | 90 / 90 / 140 |
| 6 | 🌶️ Chili Pepper | Burns every bug in a cone in front of it | ✗ | 110 / 110 / 170 |
| 7 | 🌻 Sunflower | Doesn't attack; earns 15 / 28 / 45 coins per wave | – | 75 / 80 / 120 |
| 8 | 🌹 Thorn Rose | Thorns fly in a straight line and pierce 3 / 4 / 6 bugs | ✓ | 95 / 95 / 150 |
| 9 | 🌽 Corn Popper | Pops a kernel at each of 3 / 4 / 5 bugs at once | ✓ | 85 / 90 / 140 |
| 0 | 🍀 Lucky Clover | Doesn't attack; nearby plants fire 20 / 30 / 45% faster | – | 100 / 100 / 150 |

**Air** shows which plants can hit flyers. The plant panel shows `Air: ✓ hits flyers` / `✗ ground only`, and the shop tooltips say it too. Fully upgraded plants get orbiting golden sparkles. Clover auras don't stack: a plant gets the boost of the strongest clover covering it.

**🎯 Fire points (ambush):** select an attacking plant, press **🎯 Set fire point**, then click a spot inside its range. The plant then only shoots bugs within ~1.5 tiles of that spot, aims there while idle, and gets an **ambush bonus of +30% damage**. It's a trade-off: harder hits, but only where you chose. Best for splash and flame plants (🎃 mortar, 🌶️ chili) at busy corners. Avoid it on 🌿 frost: a slow should reach every bug in range. (These values come from a simtest sweep: smaller zones made plants hold fire so often the bonus didn't pay off.) **🎯 Clear fire point** reverts; `Esc` cancels. Not available for sunflowers or clovers.

## Bugs

| Bug | HP | Speed | Ability |
|---|---|---|---|
| 🐜 Ant | 32 | fast | None: comes in swarms |
| 🐞 Beetle | 130 | slow | Armor 3 (shrugs off weak hits) |
| 🐝 Wasp | 45 | very fast | Partly resists slowing |
| 🐌 Snail (boss) | 1500 | very slow | Armor 6, resists slowing, costs **5 lives** if it gets through |
| 🐛 Caterpillar | 160 | slow | Bursts into 3 fast Grubs when squashed |
| 🐛 Grub | 22 | fast | Only appears from caterpillars |
| 🦗 Cricket | 70 | medium | Leaps 1.6 tiles ahead every 2.5 s |
| 🦟 Mosquito | 55 | fast | Field medic: every 2 s heals nearby bugs by 12% |
| ✨🐞 Golden Beetle | 90 | very fast | Rare. Escapes harmlessly (0 lives) but pays a **60-coin jackpot** if caught |
| 🦋 Butterfly | 40 | fast | **Flying**: flutters along a sky lane |
| 🦇 Bat | 150 | medium | **Flying**, armor 1: a tougher night flyer that swoops along a sky lane |
| 🦂 Scorpion | 180 | slow | Armor 2. Every 4 s spits goo at the nearest plant within 2.2 tiles, which is **stunned for 3 s** |

HP above is the base value; each wave and map multiplies it.

- **Flyers** follow sky lanes, not the path, and are drawn above the ground with a shadow beneath them. 🎃 Mortar splash and 🌶️ Chili flames can't reach them; every other attacking plant can.
- **Scorpion goo**: a green glob arcs to the plant. A stunned plant is greyed out with a goo splat and a ring showing the time left. It can't fire; a stunned sunflower skips that wave's income, and a stunned clover gives no aura. Scorpions glow green just before they spit. **No stun-lock:** a plant can't be gooed again until 2 s after its stun ends. Keep key plants back from the path, or kill scorpions first.

## Screens and extras

- **Next-wave preview** under the Start button, e.g. `Next: 🐜×10 🐞×4`, flagging `✈️ Flyers incoming` and `⚠️ Boss!`.
- **Star ratings** per map: ★★★ no lives lost, ★★ 3 or fewer, ★ cleared. Shown on the map-cleared and end cards; your best stars are saved.
- **Boss warning** toast when a boss wave begins.
- **Boss presence**: a snail arrives with a low rumble and a burst of dust at the nest. While a boss is alive, a boss HP bar at the bottom of the board shows its name and health, with one segment per snail (`🐌 Snail ×2` in the finale).
- **Confetti** when you clear a map or win the campaign.
- **Run stats** on the end card: bugs squashed, plants planted, shots fired, coins earned.
- **Personal best** (furthest map and wave) and total stars are saved and shown on the intro card, along with a **Continue from <map>** button that starts at the furthest map you've reached, with fresh lives.
- The 🤖 **autopilot is lane-aware**: on maps with sky lanes it builds anti-air plants.

## Build, run, test

Needs CMake, Ninja and Qt 6 (Widgets, Multimedia).

```sh
cmake --preset debug && cmake --build --preset debug      # or: release
./build/debug/garden                                      # play
./build/debug/garden_simtest                              # headless campaign test, prints PASS/FAIL
```

`garden_simtest` plays the whole campaign with automatically placed, fully upgraded plants, and with the autopilot on normal coins. It also checks each plant's mechanics (poison, flames, sunflower income, pierce/volley, clover boost), the per-map waves and sky lanes, and the map-to-map rules.

Hidden options for screenshots (they save an image and quit):

```sh
./build/debug/garden --screenshot shot.png --scene play [--size 1280x800]
```

Scenes: `intro` (default), `play`, `placing`, `mapclear`, `win`, `lose`, `night`, `celebrate`, `bugs` (a busy late wave), `aim` (fire point), `newbugs` (the "New bugs!" card). A trailing digit picks the map, e.g. `bugs3`, `play5`, `newbugs4`, `mapclear2`.

Real-app smoke test: `--autopilot 3` starts the AI at 3×, and `--delay 40000` takes the screenshot after 40 s:

```sh
./build/debug/garden --autopilot 3 --screenshot smoke.png --delay 40000
```
