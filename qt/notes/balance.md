# Garden Guard++ balance report (claude2-de, 17:22)

The data comes from the headless simtest at 17:21. The autopilot plays with normal coins and real unlocks. Results are deterministic, and the build was clean with every check passing.

## 1. Per map

"Lost" is autopilot lives lost on that map, with focus fire on / off. You have 20 lives for the whole campaign.

| # | Map | Grid | Start coins | Wave HP | New bugs / plants | Mechanics | Lost (on/off) | Where the losses come from |
|---|---|---|---|---|---|---|---|---|
| 1 | Sunny Meadow | 15×10 | 180 | ×1.00–2.00 | Ant, Beetle, Wasp / Sprout, Frost, Mortar, Sunflower | basics | 0 / 0 | – |
| 2 | Apple Orchard | 16×11 | 360 | ×0.76–1.51 | Caterpillar, Butterfly / Corn, Grape Vine | first sky lane, splitting | 0 / 0 | – |
| 3 | Thorny Ridge | 17×11 | 420 | ×0.80–1.60 | Cricket, Scorpion, Snail / Mushroom, Cactus | hops, stuns, boss, rocks | 0 / 0 | – |
| 4 | Harvest Hollow | 18×12 | 500 | ×0.99–1.61 | Mosquito, Golden Beetle / Chili, Rose, Clover | healers, pond and lily pads | 2 / 2 | wave 2: beetles + scorpions + first mosquitoes |
| 5 | Moonlit Garden | 20×13 | 500 | ×0.92–1.78 | first wild bats (2) | night, pond | 0 / 0 | – |
| 6 | Amber Woods | 22×15 | 600 | ×1.03–1.77 | – | wind gusts (12 s), boulders, bigger swarms | 1 / 2 | wave 3: wasp swarm + butterflies during gusts |
| 7 | Pumpkin Patch | 24×16 | 660 | ×1.15–1.92 | – | **Crossroads** (2 entrances), wind | 2 / 2 | wave 3: wasp swarm split across both entrances |
| 8 | Frost Fen | 25×17 | 740 | ×1.02–1.66 | – | snow (ground bugs ×0.85, frost ×0.8), backtrack | 0 / 0 | – |
| 9 | Snowdrift Hollow | 27×18 | 1100 | ×0.54–0.86 | – | snow, 3 lanes, pond, 2 backtracks | 2 / 2 | wave 1: the opening rush, before the defence is built |
| 10 | Evergreen Summit | 30×20 | 960 | ×1.31–2.05 | – | snow, 3 lanes, 3 snails | 4 / 3 | wave 1: opening rush (−3); wave 4: late leak, likely a bat (−1) |

Totals: 11 lost with focus fire (ends with 9 lives), 11 lost without. Focus fire saves a life mid-campaign but loses one more in the finale.

### Update 17:25: climax pass (maps 6–10)

On maps 6–10, waves 4–5 now have **×1.3 swarm size** (bosses, bats and the golden beetle keep their counts) and **×1.1 HP**. These are the `CLIMAX_N` / `CLIMAX_HP` defaults in `gen10.py`.

| Map | Lives before → after | Loss waves before | Loss waves after |
|---|---|---|---|
| 6 Amber Woods | 17 → 16 | w3 −1 | w3 −1, **w4 −1** |
| 7 Pumpkin Patch | 15 → 14 | w3 −2 | w3 −2 |
| 8 Frost Fen | 15 → 14 | – | – |
| 9 Snowdrift Hollow | 13 → 12 | w1 −2 | w1 −2 |
| 10 Evergreen Summit | won 9 → **won 6** | w1 −3, w4 −1 | w1 −3, **w4 −3** |

Sweep findings:
- **Gentler openings on maps 9–10 backfire.** At ×0.6 or ×0.8 openings, the smaller early waves pay less income, and map 10's wave 3 collapses (lost or 0 lives). Openings are unchanged.
- **The climax has a cliff just above ×1.3.** At ×1.3/1.2 the campaign is lost, and at ×1.35/1.15 it wins with 4 lives (losses at w4–5 on maps 6, 7 and 10). ×1.3/1.1 keeps a 6-life margin.
- **The wave 1 losses on maps 9–10 remain.** They come from the opening rush before the defence is built. Only start coins move them; swarm size doesn't.

## 2. Knobs and cliffs

- **Wave HP (per-map scale)** is the main knob, and it's cliff-shaped, not smooth:
  - Map 2 at ×1.15 (instead of ×0.9) **loses the campaign outright**.
  - Maps 6–8 one notch harder (×1.25 / 1.3 / 1.1) cost 6 more lives.
- **The finale is insensitive to HP.** Scaling ×0.95–1.25 gives the same result, because its losses happen in wave 1, before plants are up. To change the finale, tune its **start coins** or the **size of its first wave**, not HP.
- **Start coins** fix early-wave spikes. Map 9 lost 8 lives at 740 coins and only 2 at 1100. Map 9's low wave HP (×0.54–0.86) is a leftover of that fix and could probably go back up now.
- **Swarm size** on maps 6–10 is a template, ×1.25 → ×2.25 bugs with tighter gaps. It drives the wave 3 wasp losses.
- **Autopilot changes move everything.** claude2-0c's unlock-aware autopilot shifted results by ±10 lives. Re-tune after any autopilot change.

## 3. Honest read and next steps

- **Probably too easy for a decent human after map 3.** The autopilot is weak:
  - a fixed build order
  - greedy tile scoring
  - no fire points or ambush
  - no saving for big buys

  Even so, it ends with 9 lives. The strong-defence test (fully upgraded plants, 100k coins) never loses a life. Maps 1–3 cost the autopilot 0 lives, which suits teaching maps.
- **Pressure is front-loaded.** Nearly every loss comes in waves 1–3. Waves 4–5 rarely cost anything, so the climactic waves feel easy. Suggestions:
  1. Make waves 4–5 bigger on maps 6–10, about +30% count.
  2. Make the opening waves on maps 9–10 a little smaller, so the challenge isn't "were you set up yet".
- **Add difficulty modes** (Easy / Normal / Hard) as a global HP and coin multiplier. The cliffs make one setting a poor fit for everyone.
- **Playtest with a human** on maps 6, 7 and 10 (wind, Crossroads, finale), then re-run `garden_simtest` and keep Test 3 at 5–13 lives.
- Tools: my scratchpad has `gen10.py` (writes MAPS from tunable per-map scales and coins) and `tune.sh` (one-line sweep results). Ask me, or copy them into `tools/` if they should live in the repo.
