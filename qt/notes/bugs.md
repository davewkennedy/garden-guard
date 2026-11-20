# New bugs (claude2-63)

All in `src/data.h` (EnemyType enum, appended in order). Every EnemyDef now has a one-line `desc` for the "New bug!" cards.

## Round 1: ground bugs with abilities

| EnemyType | Emoji | Ability | hp / speed / armor / reward / lives |
|---|---|---|---|
| `Caterpillar` | 🐛 (1.25x size) | On death it bursts (goo splat) into 3 `Grub`s that inherit its wave/map hp multiplier | 160 / 0.75 / 1 / 8 / 1 |
| `Grub` | 🐛 (0.6x size) | Spawn-only (from caterpillars). Don't put it in waves | 22 / 1.9 / 0 / 2 / 1 |
| `Cricket` | 🦗 | Leaps 1.6 tiles ahead every 2.5 s (arc plus dust puff) | 70 / 1.1 / 0 / 8 / 1 |
| `Mosquito` | 🦟 | Heals bugs within 1.6 tiles by 12% max hp every 2 s (green pulse) | 55 / 1.6 / 0 / 10 / 1 |
| `GoldenBeetle` | ✨🐞 | Rare and fast. 0 lives if it escapes. Kill = 60-coin JACKPOT | 90 / 2.6 / 2 / 60 / 0 |

Sounds (via `Game::bugSound`): `bug_hop`, `bug_heal`, `bug_split`, `jackpot`.

## Round 2: flyers and a bug that fires back

| EnemyType | Emoji | Ability | hp / speed / armor / reward / lives |
|---|---|---|---|
| `Butterfly` | 🦋 | **Flying.** Follows a sky lane with a side-to-side flutter | 40 / 1.7 / 0 / 6 / 1 |
| `Bat` | 🦇 | **Flying.** Tougher night flyer, swoops up and down | 150 / 1.3 / 1 / 12 / 1 |
| `Scorpion` | 🦂 (1.1x) | Every 4 s spits goo at the nearest unstunned plant within 2.2 tiles; the plant is stunned for 3 s | 180 / 0.8 / 2 / 14 / 1 |

**Flying rule** (`EnemyDef::flying`, `Game::canHit`): mortar splash and chili flame can't hit flyers; every other plant can. Flyers get round-robin lanes from `map().skyLanes` as they spawn (`Enemy::lane`, px polylines in `Game::skyLanes`). A map with no lanes means they walk the path. `Enemy::dist` is px along its own lane, so "first" targeting stays roughly right, and a flyer at the end of its lane leaks normally.
*Player decision:* on flyer maps you can't rely on mortar/chili alone, so mix in sprouts, cactus, frost, thorn and corn near the lanes.

**Scorpion** (`EnemyDef::spitEvery/spitRange/stunTime`, `Game::spits`, `Game::updateSpits`, `Tower::stunnedUntil`): the goo is a lobbed enemy projectile (0.55 s). On impact `stunnedUntil` is set and `updateTowers` skips the plant. If the plant was sold mid-flight, the goo does nothing. Sounds: `bug_spit` on launch, `bug_split` on the splat.
*Player decision:* kill scorpions early (cactus, frost), or keep key plants more than 2.2 tiles from the path and let cheap plants soak up the goo.

**Rendering:** flyers are drawn after ground bugs, about 16 px up, with a small faint shadow on the lane, a pale halo (readable at night), a wing-flap squash, and the HP bar above the sprite. A stunned plant gets a grey wash, a goo splat on its top-left, a drip, and a green time-left ring with a dark backing.

**Fairness and readability (late round 2):**
- No stun-lock: a plant can't be gooed again until 2 s after its last stun ends, or while goo is already flying at it.
- Scorpions glow green for 0.45 s before they spit (a wind-up warning).
- A gooed sunflower skips that wave's harvest. A gooed clover gives no aura, and its ripple is hidden.
- `Game::visualY(e)` (16 px for flyers) is used by the renderer and as the homing hit point, so shots visibly hit the sprite. Targeting is unchanged.

## Known / next

- simtest (15:51): the autopilot loses Apple Orchard (wave 5, lots of butterflies). The autopilot should prefer anti-air plants on maps with lanes, or that map needs fewer butterflies early.
- Projectiles aim at the flyer's ground point (e.x, e.y), so shots look like they hit just below the sprite. Next step: a `Game::bugDrawY` helper for both the renderer and shot visuals.
- Possible next: a stunned clover/sunflower still gives its aura/income (kept simple on purpose).
