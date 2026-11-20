# Garden Guard++ 🌱

A cozy tower-defense game (C++20 / Qt 6). Bugs march along the garden path, or fly along sky lanes, toward your house. Plant defenders to stop them. Clear **10 maps × 5 waves**, from spring through summer and autumn into winter, to win the campaign.

## How to play

- Pick a plant from the shop (click it or press its key), then click a grass tile to plant it. Plants can't go on the path, boulders or open water, but they can go on lily pads.
- Click a plant to see its stats, upgrade it (3 levels), sell it (70% refund), set a fire point or focus fire.
- Squashed bugs pay coins. Clearing wave *n* pays 20 + 5·*n* coins.
- A bug that reaches the house costs lives (a snail costs 5). You have **20 lives for the whole campaign**: they carry over between maps, while coins and plants reset.

## Controls

| Key | Action |
|---|---|
| `1`–`9`, `0`, `-` | Select a plant in shop order (press again to cancel) |
| Click | Place the selected plant / select a planted one / pick a focus target |
| Double-click a plant | Select **every plant of that type** (for focus fire or Upgrade all) |
| `T` / 🎯 | Set (or clear) a fire point for the selected plant |
| `N` | Start the next wave |
| `A` / 🤖 | Autopilot: the AI plays at 2× while you watch (press again to take over) |
| `F` | Speed 1× → 2× → 3× |
| `Space` | Pause / resume |
| `J` / 📖 | Garden Journal (open / close) |
| `Esc` | Cancel placing, aiming or focus fire; deselect |

## Campaign

Boards grow from 15×10 to 30×20 tiles as the campaign goes on. Plants and bugs are drawn a little larger on the big boards so they stay readable.

| # | Map | Season | Grid | Start coins | New bugs | New plants | Sky lanes | Special |
|---|---|---|---|---|---|---|---|---|
| 1 | Sunny Meadow | 🌸 spring | 15×10 | 180 | 🐜 Ant, 🐞 Beetle, 🐝 Wasp | 🌱 🌿 🎃 🌻 | 0 | the basics |
| 2 | Apple Orchard | 🌸 spring | 16×11 | 360 | 🐛 Caterpillar, 🦋 Butterfly | 🌽 Corn, 🍇 Grape | 1 | first flyers |
| 3 | Thorny Ridge | ☀️ summer | 17×11 | 420 | 🦗 Cricket, 🦂 Scorpion, 🐌 Snail | 🌵 Cactus, 🍄 Mushroom | 1 | 🪨 boulders, first boss |
| 4 | Harvest Hollow | ☀️ summer | 18×12 | 500 | 🦟 Mosquito, ✨🐞 Golden Beetle | 🌶️ Chili, 🌹 Rose, 🍀 Clover | 2 | 🪷 pond with lily pads |
| 5 | Moonlit Garden | ☀️ summer | 20×13 | 500 | 🦇 Bat (wildcard) | – | 2 | 🌙 **night**, fireflies, pond |
| 6 | Amber Woods | 🍂 autumn | 22×15 | 600 | – | – | 2 | long switchbacks, 🪨 boulders, 🌬️ wind |
| 7 | Pumpkin Patch | 🍂 autumn | 24×16 | 660 | – | – | 2 | 🔀 **Crossroads**: two entrances (left and top), huge swarms, 🌬️ wind |
| 8 | Frost Fen | ❄️ winter | 25×17 | 740 | – | – | 2 | ❄️ deep snow, path backtracks, icy pond, 2 snails |
| 9 | Snowdrift Hollow | ❄️ winter | 27×18 | 1100 | – | – | 3 | ❄️ deep snow, path backtracks, icy pond, 2 snails |
| 10 | Evergreen Summit | ❄️ winter | 30×20 | 960 | – | – | 3 | ❄️ deep snow, longest path, 3 snails |

Bug health is each bug's base HP times the wave's multiplier, and it climbs every wave. **Sky lanes** are the dotted trails with chevrons that flyers follow; plan your anti-air around them.

**Plant unlocks:** a plant stays hidden from the shop until its map. Starting a map that introduces bugs or plants shows a combined **"New bugs… and new plants"** card. Each new plant is listed with the bug it counters, and wears a gold NEW ribbon in the shop on that map. *Play again* relocks everything back to map 1's plants; *Continue* and *Choose map* unlock everything up to the starting map.

**🗺️ On this map:** the same card also lists the map's special rules, so nothing invisible catches you out: 🌙 night, 🦇 wild bats, 🌬️ wind gusts, ❄️ deep snow, 🪷 ponds, 🪨 boulders and 🔀 two entrances. Maps with no new bugs or plants still show it, titled *"<Map>: watch out!"*.

### Map mechanics

- **🪨 Boulders** block planting.
- **🪷 Ponds**: open water can't be planted, but the **lily pads** on it can. On winter maps the ponds are frozen (icy look, same rules).
- **❄️ Deep snow** (maps 8-10): ground bugs move at **×0.85** speed (flyers don't care), but the cold makes 🌿 frost slows weaker (**×0.8**).
- **🌬️ Wind gusts** (maps 6-7, every ~12 s): a gust is announced 1 s ahead, then for ~2.5 s it pushes airborne flyers sideways off their route. Flyers held down by a 🍇 Grape Vine are immune.
- **🔀 Two entrances** (map 7, *Crossroads*): a second nest at the top edge feeds a second path that crosses the first on its way to the house, and ground bugs alternate between the two entrances. Plants at a crossing hit both streams. Caterpillar grubs stay on their parent's path; flyers are unaffected. Neither path can be planted on.

**Seasons:** each map has a season with its own look: spring blossoms and drifting petals, bright summer flowers, autumn leaf piles with tumbling leaves, and winter snow drifts, frost, snowbanks along the path, icy ponds and gentle snowfall. The weather stays small and translucent, so bugs and plants are always clear.

## Plants

Cost: plant → level 2 → level 3.

| Plant | Unlocks | Role | Air | Cost |
|---|---|---|---|---|
| 🌱 Pea Sprout | map 1 | Cheap, fast single shots | ✓ | 50 / 60 / 100 |
| 🌿 Frost Fern | map 1 | Slows bugs 40–60% | ✓ | 70 / 70 / 110 |
| 🎃 Pumpkin Mortar | map 1 | Lobbed shells splash groups | ✗ | 100 / 100 / 160 |
| 🌻 Sunflower | map 1 | No attack; +15 / 28 / 45 coins per wave | – | 75 / 80 / 120 |
| 🌽 Corn Popper | map 2 | A kernel at each of 3 / 4 / 5 bugs | ✓ | 85 / 90 / 140 |
| 🍇 Grape Vine | map 2 | **Lassos a flyer and grounds it** for 2.5 / 3 / 3.5 s, so mortars and chilis can hit it too | ✓ | 90 / 90 / 130 |
| 🌵 Cactus Sniper | map 3 | Long range, heavy hits; **+50% vs scorpions**, outranges their goo | ✓ | 125 / 120 / 180 |
| 🍄 Spore Mushroom | map 3 | Poison that ignores armor (great on snails) | ✓ | 90 / 90 / 140 |
| 🌶️ Chili Pepper | map 4 | Burns every bug in a cone | ✗ | 110 / 110 / 170 |
| 🌹 Thorn Rose | map 4 | Thorns pierce 3 / 4 / 6 bugs in a line | ✓ | 95 / 95 / 150 |
| 🍀 Lucky Clover | map 4 | No attack; nearby plants fire 20 / 30 / 45% faster (doesn't stack) | – | 100 / 100 / 150 |

Fully upgraded plants get orbiting golden sparkles.

### 🎯 Focus fire

Double-click a plant to select every plant of its type (or select a single plant), then **click a bug**: they all gang up on it. Focus ends when the bug dies or escapes; `Esc` cancels. A group selection also gets **Upgrade all**. A plant with a fire point leaves its ambush zone to chase the focus target, but without the ambush bonus.

### 🎯 Fire points (ambush)

Select an attacking plant, press **Set fire point** (`T`), then click a spot inside its range. It then only shoots bugs within ~1.5 tiles of that spot, for **+30% damage**. This works best for 🎃 and 🌶️ at busy corners; avoid it on 🌿 frost. Not available for sunflowers or clovers.

## Bugs

| Bug | HP | Speed | Ability |
|---|---|---|---|
| 🐜 Ant | 32 | fast | Comes in swarms |
| 🐞 Beetle | 130 | slow | Armor 3 |
| 🐝 Wasp | 45 | very fast | Partly resists slowing |
| 🐛 Caterpillar | 160 | slow | Bursts into 3 fast 🐛 Grubs (22 HP) |
| 🦋 Butterfly | 40 | fast | **Flying**, follows a sky lane |
| 🦗 Cricket | 70 | medium | Leaps 1.6 tiles ahead every 2.5 s |
| 🦂 Scorpion | 180 | slow | Armor 2; every 4 s spits goo that **stuns a plant for 3 s** (glows green first) |
| 🐌 Snail (boss) | 1500 | very slow | Armor 6, resists slowing, costs **5 lives** |
| 🦟 Mosquito | 55 | fast | Heals nearby bugs 12% every 2 s |
| ✨🐞 Golden Beetle | 90 | very fast | Escapes harmlessly, but pays a **60-coin jackpot** if caught |
| 🦇 Bat | 150 | medium | **Wildcard flyer**: armor 1, flies its own random route (no lane, red eyes, purple trail); pays **30 coins**, costs 1 life |

Flyers are drawn above the ground with a shadow. 🎃 splash and 🌶️ flames can't reach them unless a 🍇 Grape Vine pulls them down. A stunned plant is greyed out with a goo splat and a timer ring, and it can't be gooed again until 2 s after the stun ends.

## Screens and extras

- **Next-wave preview** under the Start button, flagging flyers and bosses.
- **Boss warning** toast, a dust burst at the nest, and a boss HP bar with one segment per snail.
- **Stars** per map: ★★★ no lives lost, ★★ 3 or fewer, ★ cleared. Best stars are saved.
- **Intro card**: personal best, total stars, **Continue from <map>**, and **🗺️ Choose map**. Choose map lists every map you've reached, with its season and best stars, plus a **Play** button that starts a fresh run there with full lives and that map's plants unlocked.
- **📖 Garden Journal** (`J`): every bug met so far (HP, speed, armor, reward, and flying / boss / wildcard tags) and every plant unlocked so far (with its counter tip). Unmet entries show as `???` teasers. The game pauses while it's open.
- **Confetti** on map clears and on the win; **run stats** on the end card.
- The 🤖 **autopilot** is lane-aware and builds anti-air on maps with sky lanes.

## Build, run, test

Needs CMake, Ninja and Qt 6 (Widgets, Multimedia).

```sh
cmake --preset debug && cmake --build --preset debug      # or: release
./build/debug/garden                                      # play
./build/debug/garden_simtest                              # headless campaign test, prints PASS/FAIL
```

`garden_simtest` plays the campaign with automatically placed plants and with the autopilot, and checks plant mechanics, waves, sky lanes and the map-to-map rules.

Hidden options:

| Option | Effect |
|---|---|
| `--screenshot FILE` | Save a screenshot and quit (with `--scene`, `--size`, `--delay`) |
| `--scene NAME` | Scene to capture (see below; default `intro`) |
| `--size WxH` | Window size for the screenshot, e.g. `1280x800` |
| `--delay MS` | Milliseconds before the screenshot is taken (default 800) |
| `--autopilot S` | Start with the 🤖 autopilot at speed S (1-3) |
| `--start-map N` | Start a fresh run on map N (1-based), with its plants unlocked |

```sh
./build/debug/garden --screenshot shot.png --scene play [--size 1280x800]
./build/debug/garden --autopilot 3 --screenshot smoke.png --delay 40000   # real-app smoke test
./build/debug/garden --start-map 8                                        # jump straight to Frost Fen
```

Scenes: `intro` (default), `play`, `placing`, `mapclear`, `win`, `lose`, `night`, `celebrate`, `bugs` (a busy final wave), `aim` (fire point), `focus` (focus fire), `newbugs` (the new bugs/plants card), `mapselect` (the Choose map card), `journal` (the Garden Journal, e.g. `journal4`). Trailing digits pick the map, including multi-digit ones: `bugs3`, `play10`, `newbugs4`.
