// Garden Guard: game balance and map data (ported from js/data.js).
#pragma once

#include <QString>
#include <array>
#include <vector>

namespace data {

// Tiles are always 64 logical px. Each map has its own grid size (MapDef::cols/rows);
// the board scales to fit the window, so bigger grids look like smaller tiles.
// Runtime grid size: Game::cols/rows/W/H.
constexpr int TILE = 64;
constexpr int DEFAULT_COLS = 15;
constexpr int DEFAULT_ROWS = 10;

constexpr double SELL_RATE = 0.7;      // refund = floor(spent * SELL_RATE)
constexpr int WAVE_BONUS_BASE = 20;    // coins on clearing wave n: BASE + PER * n
constexpr int WAVE_BONUS_PER = 5;
constexpr double MORTAR_FLIGHT = 0.7;  // seconds a mortar shell is in the air
constexpr double FLAME_HALF_ANGLE = 0.6;  // radians (~34°) each side of the chili's aim

constexpr int START_LIVES = 20;   // lives carry over from map to map

// (Campaign maps, MapDef/MAPS, are at the end of this file: they include per-map waves.)

// How a plant attacks.
enum class Projectile {
    Bolt,     // homing pea
    Frost,    // homing, slows
    Mortar,   // lobbed shell, splash damage where it lands
    Spike,    // fast homing needle
    Spore,    // homing, poisons (damage over time, ignores armor)
    Flame,    // instant cone: hits every bug in front of it
    Thorn,    // straight piercing shot: hits up to `pierce` bugs along a line
    Kernel,   // homing popcorn; the tower fires one at each of `shots` targets
    Aura,     // doesn't attack; speeds up nearby plants (clover)
    None,     // doesn't attack (sunflower)
    Vine,     // lassos a flyer and grounds it for a while (grape vine)
};

// Units: range/splash in tiles, rate in shots/second, poisonDps in hp/second.
// levels[0].cost = price to plant; levels[n].cost = price to upgrade to level n.
// Fields not given stay 0 (designated initializers, C++20).
struct TowerLevel {
    int cost = 0;
    int damage = 0;
    double range = 0;
    double rate = 0;
    double slow = 0;        // frost: speed reduction 0..1
    double slowTime = 0;    // frost: seconds
    double splash = 0;      // mortar: radius in tiles
    double poisonDps = 0;   // mushroom: poison damage per second
    double poisonTime = 0;  // mushroom: seconds
    int income = 0;         // sunflower: coins at the end of each wave
    int pierce = 0;         // thorn rose: bugs one thorn can pass through
    int shots = 0;          // corn popper: targets per volley
    double boost = 0;       // clover: fire-rate bonus for plants in range (0.2 = +20%)
    double groundTime = 0;  // grape vine: seconds a lassoed flyer stays grounded
};

struct TowerDef {
    QString id;           // also the sound suffix: "shoot_" + id
    QString name;
    QString icon;
    Projectile projectile;
    double projectileSpeed;   // tiles/second (homing kinds)
    QString desc;
    QString color;
    std::vector<TowerLevel> levels;
    // claude2-0c: plant unlocks. The plant appears in the shop from this map on (0-based
    // campaign index); earlier maps hide it entirely. `counters` is its "New plants" card line.
    int unlockMap = 0;
    QString counters = {};
};

// Shop order (keys 1-7).
inline const std::vector<TowerDef> TOWERS = {
    {"sprout", "Pea Sprout", QStringLiteral("🌱"), Projectile::Bolt, 9,
     "Fast single shots.", "#5cb85c",
     {{.cost = 50, .damage = 12, .range = 2.6, .rate = 1.6},
      {.cost = 60, .damage = 20, .range = 2.9, .rate = 1.9},
      {.cost = 100, .damage = 34, .range = 3.2, .rate = 2.3}}},
    {"frost", "Frost Fern", QStringLiteral("🌿"), Projectile::Frost, 8,
     "Chills bugs to slow them.", "#5bc0de",
     {{.cost = 70, .damage = 4, .range = 2.3, .rate = 1.2, .slow = 0.4, .slowTime = 1.5},
      {.cost = 70, .damage = 7, .range = 2.6, .rate = 1.4, .slow = 0.5, .slowTime = 1.8},
      {.cost = 110, .damage = 12, .range = 2.9, .rate = 1.6, .slow = 0.6, .slowTime = 2.2}}},
    {"mortar", "Pumpkin Mortar", QStringLiteral("🎃"), Projectile::Mortar, 0,
     "Lobs shells that splash groups.", "#f0883e",
     {{.cost = 100, .damage = 30, .range = 3.2, .rate = 0.6, .splash = 0.9},
      {.cost = 100, .damage = 50, .range = 3.5, .rate = 0.7, .splash = 1.0},
      {.cost = 160, .damage = 85, .range = 3.8, .rate = 0.8, .splash = 1.2}}},
    {"cactus", "Cactus Sniper", QStringLiteral("🌵"), Projectile::Spike, 20,
     "Long range: picks off scorpions before they can spit (+50% damage to them).", "#3d8b3d",
     {{.cost = 125, .damage = 70, .range = 5.0, .rate = 0.5},
      {.cost = 120, .damage = 120, .range = 5.5, .rate = 0.55},
      {.cost = 180, .damage = 200, .range = 6.0, .rate = 0.6}},
     2, QStringLiteral("snipes 🦂 scorpions from beyond their goo range")},
    {"mushroom", "Spore Mushroom", QStringLiteral("🍄"), Projectile::Spore, 6,
     "Poisons bugs. Ignores armor.", "#b06ad9",
     {{.cost = 90, .damage = 4, .range = 2.6, .rate = 0.9, .poisonDps = 9, .poisonTime = 3},
      {.cost = 90, .damage = 6, .range = 2.8, .rate = 1.0, .poisonDps = 16, .poisonTime = 3.5},
      {.cost = 140, .damage = 10, .range = 3.0, .rate = 1.1, .poisonDps = 28, .poisonTime = 4}},
     2, QStringLiteral("poison melts the armored 🐌 snail boss")},
    {"chili", "Chili Pepper", QStringLiteral("🌶️"), Projectile::Flame, 0,
     "Burns every bug in a cone.", "#e5402b",
     {{.cost = 110, .damage = 14, .range = 1.7, .rate = 1.2},
      {.cost = 110, .damage = 22, .range = 1.9, .rate = 1.3},
      {.cost = 170, .damage = 36, .range = 2.1, .rate = 1.5}},
     3, QStringLiteral("torches the 🦟 mosquito-healed swarm all at once")},
    {"sunflower", "Sunflower", QStringLiteral("🌻"), Projectile::None, 0,
     "Earns coins every wave.", "#f2b705",
     {{.cost = 75, .income = 15},
      {.cost = 80, .income = 28},
      {.cost = 120, .income = 45}}},
    {"rose", "Thorn Rose", QStringLiteral("🌹"), Projectile::Thorn, 11,
     "Thorns pierce a line of bugs.", "#d6336c",
     {{.cost = 95, .damage = 16, .range = 3.0, .rate = 0.9, .pierce = 3},
      {.cost = 95, .damage = 26, .range = 3.3, .rate = 1.0, .pierce = 4},
      {.cost = 150, .damage = 42, .range = 3.6, .rate = 1.1, .pierce = 6}},
     3, QStringLiteral("thorns skewer 🦇 bats and the bugs behind them")},
    {"corn", "Corn Popper", QStringLiteral("🌽"), Projectile::Kernel, 10,
     "Pops kernels at several bugs.", "#f5c518",
     {{.cost = 85, .damage = 9, .range = 2.5, .rate = 0.9, .shots = 3},
      {.cost = 90, .damage = 14, .range = 2.7, .rate = 1.0, .shots = 4},
      {.cost = 140, .damage = 22, .range = 2.9, .rate = 1.1, .shots = 5}},
     1, QStringLiteral("pops a whole brood of 🐛 grubs at once")},
    {"clover", "Lucky Clover", QStringLiteral("🍀"), Projectile::Aura, 0,
     "Nearby plants fire faster.", "#2fb36b",
     {{.cost = 100, .range = 1.6, .boost = 0.2},
      {.cost = 100, .range = 1.8, .boost = 0.3},
      {.cost = 150, .range = 2.1, .boost = 0.45}},
     3, QStringLiteral("speeds up your plants to catch the ✨🐞 golden beetle")},
    {"grape", "Grape Vine", QStringLiteral("🍇"), Projectile::Vine, 0,
     "Lassos flyers and pulls them down so every plant can hit them.", "#7b3fa0",
     {{.cost = 90, .damage = 6, .range = 2.5, .rate = 0.33, .groundTime = 2.5},
      {.cost = 90, .damage = 9, .range = 2.6, .rate = 0.36, .groundTime = 3.0},
      {.cost = 130, .damage = 14, .range = 2.8, .rate = 0.4, .groundTime = 3.5}},
     1, QStringLiteral("pulls 🦋 flyers down where mortars and chilis can reach them")},
};

// Which way each bug emoji faces as drawn: side views face left or right;
// the ladybug is a top-down view facing up.
enum class Facing { Left, Right, Up };

// speed in tiles/second; armor = flat damage reduction (min 1 damage);
// slowResist 0..1 reduces frost slow; lives = lives lost if it reaches the house.
struct EnemyDef {
    QString name;
    QString icon;
    double hp;
    double speed;
    int reward;
    int lives;
    double armor;
    double slowResist;
    bool boss;
    Facing facing;
    // Bug abilities (all off by default so the older positional entries still compile).
    double scale = 1.0;      // sprite size multiplier
    int splitInto = -1;      // EnemyType spawned on death (-1 = none)
    int splitCount = 0;
    double hopEvery = 0;     // seconds between hops (0 = never hops)
    double hopDist = 0;      // tiles skipped forward per hop
    double healEvery = 0;    // seconds between heal pulses (0 = no healing)
    double healRadius = 0;   // tiles
    double healPct = 0;      // fraction of each nearby bug's max hp restored
    bool golden = false;     // shiny jackpot bug: sparkles, big coin burst
    bool flying = false;     // follows a map sky lane; mortar splash and chili flame can't reach it
    QString desc = {};       // one-line "New bug!" card text
    double spitEvery = 0;    // scorpion: seconds between goo spits at plants (0 = never)
    double spitRange = 0;    // tiles
    double stunTime = 0;     // seconds a goo-hit plant can't act
    bool wild = false;       // wildcard flyer: wanders its own route to the house instead of a lane
};

enum EnemyType { Ant, Beetle, Wasp, Snail, Caterpillar, Grub, Cricket, Mosquito, GoldenBeetle,
                 Butterfly, Bat, Scorpion };

// Positional: name, icon, hp, speed, reward, lives, armor, slowResist, boss, facing,
// scale, splitInto, splitCount, hopEvery, hopDist, healEvery, healRadius, healPct, golden,
// flying, desc, spitEvery, spitRange, stunTime.
inline const std::vector<EnemyDef> ENEMIES = {
    {"Ant", QStringLiteral("🐜"), 32, 1.5, 5, 1, 0, 0, false, Facing::Left,
     1.0, -1, 0, 0, 0, 0, 0, 0, false, false, "Quick little marcher. Comes in crowds."},
    {"Beetle", QStringLiteral("🐞"), 130, 0.9, 11, 1, 3, 0, false, Facing::Up,
     1.0, -1, 0, 0, 0, 0, 0, 0, false, false, "Armored shell shrugs off weak hits. Poison ignores it."},
    {"Wasp", QStringLiteral("🐝"), 45, 2.4, 7, 1, 0, 0.2, false, Facing::Left,
     1.0, -1, 0, 0, 0, 0, 0, 0, false, false, "Very fast. Hard to chill."},
    {"Snail", QStringLiteral("🐌"), 1500, 0.45, 120, 5, 6, 0.5, true, Facing::Right,
     1.0, -1, 0, 0, 0, 0, 0, 0, false, false, "Boss! Slow, heavily armored, costs 5 lives."},
    // Caterpillar: chubby and slow; bursts into 3 fast grubs when it dies.
    {"Caterpillar", QStringLiteral("🐛"), 160, 0.75, 8, 1, 1, 0, false, Facing::Left,
     1.25, Grub, 3, 0, 0, 0, 0, 0, false, false, "Bursts into 3 fast grubs when squashed."},
    // Grub: what a caterpillar splits into. Small, fast, fragile.
    {"Grub", QStringLiteral("🐛"), 22, 1.9, 2, 1, 0, 0, false, Facing::Left,
     0.6, -1, 0, 0, 0, 0, 0, 0, false, false, "Baby caterpillar. Tiny and quick."},
    // Cricket: every 2.5 s it leaps 1.6 tiles ahead along the path.
    {"Cricket", QStringLiteral("🦗"), 70, 1.1, 8, 1, 0, 0.3, false, Facing::Left,
     1.0, -1, 0, 2.5, 1.6, 0, 0, 0, false, false, "Leaps ahead every few seconds, skipping your defenses."},
    // Mosquito: field medic. Every 2 s heals bugs within 1.6 tiles by 12% of their max hp.
    {"Mosquito", QStringLiteral("🦟"), 55, 1.6, 10, 1, 0, 0.1, false, Facing::Left,
     1.0, -1, 0, 0, 0, 2.0, 1.6, 0.12, false, false, "Heals nearby bugs. Swat it first!"},
    // Golden Beetle: rare, quick, slips away harmlessly (0 lives), but pays a 60-coin jackpot.
    {"Golden Beetle", QStringLiteral("🐞"), 90, 2.6, 60, 0, 2, 0.6, false, Facing::Up,
     1.1, -1, 0, 0, 0, 0, 0, 0, true, false, "Rare and speedy. Catch it for a 60-coin jackpot!"},
    // Butterfly: fragile flyer that flutters along a sky lane.
    {"Butterfly", QStringLiteral("🦋"), 40, 1.7, 6, 1, 0, 0, false, Facing::Up,
     1.0, -1, 0, 0, 0, 0, 0, 0, false, true, "Flies over the path. Mortars and chilies can't reach it."},
    // Bat: tougher night flyer that swoops along a sky lane.
    // Bat: a wildcard. Flies its own wandering route (not a lane) and always reaches the house in time.
    {"Bat", QStringLiteral("🦇"), 150, 1.3, 30, 1, 1, 0.2, false, Facing::Up,
     1.0, -1, 0, 0, 0, 0, 0, 0, false, true,
     "Wild flyer: zig-zags its own route to your house. Catch it for 30 coins!", 0, 0, 0, true},
    // Scorpion: spits goo at the nearest plant, stunning it.
    {"Scorpion", QStringLiteral("🦂"), 180, 0.8, 14, 1, 2, 0, false, Facing::Left,
     1.1, -1, 0, 0, 0, 0, 0, 0, false, false, "Spits goo that stuns the nearest plant for 3 s.",
     4.0, 2.2, 3.0},
};

// Each group: count enemies of a type, first at `delay` s after the wave starts, then every `gap` s.
struct WaveGroup {
    EnemyType enemy;
    int count;
    double gap;
    double delay;
};

struct Wave {
    double hpMult;
    std::vector<WaveGroup> groups;
};

inline const std::vector<Wave> WAVES = {
    {1.00, {{Ant, 10, 0.9, 0}}},
    {1.20, {{Ant, 16, 0.7, 0}}},
    {1.40, {{Ant, 10, 0.8, 0}, {Beetle, 4, 2.0, 4}}},
    {1.65, {{Wasp, 12, 0.5, 0}, {Ant, 6, 0.8, 5}, {Caterpillar, 2, 3.0, 8}}},
    {1.90, {{Beetle, 7, 1.4, 0}, {Ant, 10, 0.6, 2}, {Cricket, 4, 1.8, 6}}},
    {2.20, {{Wasp, 18, 0.35, 0}, {Cricket, 4, 1.5, 4}, {GoldenBeetle, 1, 1, 8}}},
    {2.50, {{Beetle, 10, 1.1, 0}, {Ant, 12, 0.5, 3}, {Mosquito, 2, 4.0, 6}, {Wasp, 8, 0.45, 9},
            {Caterpillar, 2, 3.0, 12}}},
    {2.85, {{Ant, 14, 0.45, 0}, {Cricket, 4, 1.2, 2}, {Snail, 1, 1, 6}, {Mosquito, 1, 1, 7}}},
    {3.25, {{Beetle, 13, 0.9, 0}, {Wasp, 18, 0.35, 4}, {Mosquito, 2, 4.0, 6}, {Caterpillar, 4, 2.0, 10}}},
    {3.70, {{Ant, 18, 0.35, 0}, {Beetle, 14, 0.8, 2}, {Cricket, 4, 1.2, 4}, {Wasp, 22, 0.3, 8},
            {Snail, 2, 6, 10}, {Mosquito, 2, 5.0, 12}, {Caterpillar, 3, 2.0, 16}, {GoldenBeetle, 1, 1, 18}}},
};

// A campaign map. The path is a list of tile waypoints joined by straight
// horizontal/vertical lines; the first and last are just off-grid (col -1 and
// col 15) so bugs walk in from the left and out at the house on the right.
using Lane = std::vector<std::array<double, 2>>;
enum class Season { Spring, Summer, Autumn, Winter };
struct MapDef {
    QString name;
    QString blurb;                              // shown before the map starts
    std::vector<std::array<int, 2>> path;
    double hpMult;                              // bug health multiplier on this map
    int startCoins;                             // coins reset to this when the map starts
    QString grassA, grassB;                     // checkerboard grass colors
    unsigned seed;                              // decoration layout
    bool night = false;                         // darker scene with drifting fireflies
    // Flying bugs follow these instead of the path: tile-coordinate polylines
    // (diagonals allowed) from off-grid left to the house on the right.
    std::vector<Lane> skyLanes = {};
    std::vector<Wave> waves = {};               // this map's waves (empty = global WAVES)
    // Grid size in tiles. The path's last waypoint should be at x = cols (just off-grid right).
    int cols = DEFAULT_COLS, rows = DEFAULT_ROWS;
    // Seasonal look (background decorations and drifting petals/leaves/snow). Stacks with `night`.
    Season season = Season::Spring;
};

inline const std::vector<MapDef> MAPS = {
    // 1: Sunny Meadow (15x10, spring)
    {"Sunny Meadow", "A gentle meadow to get your garden growing.",
     {{-1, 1}, {2, 1}, {2, 6}, {6, 6}, {6, 2}, {10, 2}, {10, 8}, {13, 8}, {13, 4}, {15, 4}},
     1.0, 180, "#79c25a", "#73bb54", 20261118, false,
     {},
     {
      {1.00, {{Ant, 10, 0.9, 0}}},
      {1.20, {{Ant, 14, 0.7, 0}, {Beetle, 2, 2.5, 6}}},
      {1.40, {{Wasp, 10, 0.5, 0}, {Ant, 8, 0.7, 5}}},
      {1.70, {{Beetle, 6, 1.4, 0}, {Ant, 10, 0.6, 2}, {Wasp, 6, 0.5, 8}}},
      {2.00, {{Ant, 20, 0.3, 0}, {Wasp, 14, 0.35, 3}, {Beetle, 6, 1, 6}}}},
     15, 10, Season::Spring},
    // 2: Apple Orchard (16x11, spring)
    {"Apple Orchard", "Butterflies flutter along the sky lane, out of reach of mortars and chilis. Caterpillars split when squashed!",
     {{-1, 9}, {3, 9}, {3, 1}, {7, 1}, {7, 8}, {12, 8}, {12, 2}, {16, 2}},
     1.0, 360, "#8cc35a", "#86bc52", 7051, false,
     {{{-1, 10}, {5, 10}, {10, 4.5}, {16, 2}}},
     {
      {0.67, {{Ant, 12, 0.7, 0}, {Caterpillar, 2, 3, 5}}},
      {0.84, {{Butterfly, 8, 0.8, 0}, {Ant, 10, 0.6, 4}}},
      {1.01, {{Beetle, 6, 1.4, 0}, {Caterpillar, 4, 2, 3}, {Butterfly, 6, 0.7, 8}}},
      {1.18, {{Wasp, 14, 0.4, 0}, {Butterfly, 10, 0.6, 4}, {Caterpillar, 3, 2.5, 8}}},
      {1.34, {{Ant, 16, 0.4, 0}, {Beetle, 8, 1, 2}, {Butterfly, 14, 0.5, 6}, {Caterpillar, 4, 2, 10}}}},
     16, 11, Season::Spring},
    // 3: Thorny Ridge (17x11, summer)
    {"Thorny Ridge", "Crickets leap ahead and scorpions spit goo that stuns plants: don't crowd the path. A snail guards the ridge.",
     {{-1, 4}, {1, 4}, {1, 1}, {4, 1}, {4, 9}, {9, 9}, {9, 3}, {12, 3}, {12, 6}, {17, 6}},
     1.0, 420, "#6aae55", "#64a74f", 4242, false,
     {{{-1, 0}, {7, 0.5}, {11, 5.5}, {17, 6}}},
     {
      {0.63, {{Cricket, 6, 1.2, 0}, {Ant, 12, 0.6, 3}}},
      {0.77, {{Scorpion, 3, 3, 0}, {Beetle, 6, 1.3, 2}, {Butterfly, 6, 0.8, 6}}},
      {0.90, {{Cricket, 8, 1, 0}, {Wasp, 12, 0.4, 4}, {Caterpillar, 3, 2.5, 8}}},
      {1.08, {{Scorpion, 5, 2.5, 0}, {Butterfly, 10, 0.6, 3}, {Cricket, 6, 1, 8}}},
      {1.26, {{Ant, 18, 0.4, 0}, {Beetle, 10, 0.9, 2}, {Snail, 1, 1, 6}, {Scorpion, 3, 3, 9}, {Butterfly, 10, 0.6, 10}}}},
     17, 11, Season::Summer},
    // 4: Harvest Hollow (18x12, summer)
    {"Harvest Hollow", "Golden late-summer fields. Mosquitoes heal the swarm, so take them out first. Watch for a golden glint!",
     {{-1, 7}, {3, 7}, {3, 2}, {7, 2}, {7, 9}, {11, 9}, {11, 1}, {15, 1}, {15, 6}, {18, 6}},
     1.0, 500, "#b5b04c", "#ada846", 1031, false,
     {{{-1, 0}, {9, 0.3}, {13, 3.5}, {18, 6}}, {{-1, 11}, {6, 10.5}, {13, 7}, {18, 6}}},
     {
      {0.90, {{Ant, 16, 0.4, 0}, {Butterfly, 8, 0.6, 3}, {Cricket, 6, 1, 6}}},
      {1.09, {{Beetle, 10, 1, 0}, {Scorpion, 3, 3, 4}, {Mosquito, 2, 4, 6}}},
      {1.28, {{Caterpillar, 6, 1.6, 0}, {Mosquito, 3, 3.5, 4}, {Wasp, 14, 0.35, 6}, {GoldenBeetle, 1, 1, 10}}},
      {1.38, {{Cricket, 10, 0.8, 0}, {Butterfly, 10, 0.6, 3}, {Scorpion, 3, 2.5, 6}}},
      {1.47, {{Ant, 20, 0.35, 0}, {Beetle, 12, 0.8, 2}, {Mosquito, 2, 4, 6}, {Snail, 1, 1, 8}, {Caterpillar, 3, 2, 12}, {Butterfly, 8, 0.6, 14}}}},
     18, 12, Season::Summer},
    // 5: Moonlit Garden (20x13, summer, night)
    {"Moonlit Garden", "Night falls. Fireflies light the way, and a wild bat flaps unpredictably across the garden. Tip: a Frost Fern where a lane crosses the path slows bugs on the ground and in the air.",
     {{-1, 1}, {15, 1}, {15, 5}, {2, 5}, {2, 10}, {20, 10}},
     1.0, 500, "#2f5f42", "#2b583d", 9001, true,
     {{{-1, 3}, {10, 3}, {17, 7.5}, {20, 10}}, {{-1, 7.5}, {11, 7.5}, {20, 10}}},
     {
      {0.84, {{Butterfly, 8, 0.7, 0}, {Ant, 14, 0.5, 2}}},
      {1.00, {{Mosquito, 2, 4, 0}, {Beetle, 8, 1.2, 0}, {Butterfly, 8, 0.6, 5}}},
      {1.21, {{Bat, 1, 1, 4}, {Cricket, 8, 1, 0}, {Butterfly, 6, 0.6, 3}, {GoldenBeetle, 1, 1, 9}}},
      {1.42, {{Scorpion, 4, 2.5, 0}, {Mosquito, 2, 3.5, 3}, {Butterfly, 8, 0.6, 6}, {Caterpillar, 3, 2, 8}}},
      {1.63, {{Beetle, 10, 0.8, 0}, {Bat, 1, 1, 6}, {Mosquito, 2, 4, 5}, {Wasp, 12, 0.35, 8}, {Snail, 1, 1, 10}}}},
     20, 13, Season::Summer},
    // 6: Amber Woods (22x15, autumn)
    {"Amber Woods", "Autumn leaves and long switchbacks. The swarms are bigger now, and a wild bat may swoop through.",
     {{-1, 2}, {4, 2}, {4, 12}, {9, 12}, {9, 2}, {14, 2}, {14, 12}, {19, 12}, {19, 7}, {22, 7}},
     1.0, 600, "#c99a45", "#c1923f", 6161, false,
     {{{-1, 14}, {7, 14}, {12, 7}, {22, 7}}, {{-1, 0}, {11, 0}, {17, 5}, {22, 7}}},
     {
      {0.77, {{Ant, 20, 0.36, 0}, {Butterfly, 8, 0.54, 3}, {Cricket, 5, 0.89, 6}}},
      {0.90, {{Beetle, 10, 0.89, 0}, {Scorpion, 2, 2.68, 4}, {Caterpillar, 4, 1.79, 6}}},
      {1.04, {{Wasp, 18, 0.31, 0}, {Mosquito, 2, 3.13, 4}, {Butterfly, 10, 0.54, 6}, {GoldenBeetle, 1, 1, 10}}},
      {1.17, {{Cricket, 10, 0.72, 0}, {Scorpion, 4, 2.24, 4}, {Caterpillar, 4, 1.79, 6}, {Butterfly, 10, 0.45, 10}}},
      {1.31, {{Ant, 22, 0.31, 0}, {Beetle, 12, 0.72, 2}, {Mosquito, 2, 3.58, 6}, {Snail, 1, 6, 8}, {Caterpillar, 4, 1.79, 12}, {Butterfly, 10, 0.45, 14}, {Bat, 1, 1, 16}}}},
     22, 15, Season::Autumn},
    // 7: Pumpkin Patch (24x16, autumn)
    {"Pumpkin Patch", "Harvest time! Huge swarms march between the pumpkins. Spread your plants out: scorpions punish crowds.",
     {{-1, 13}, {5, 13}, {5, 3}, {11, 3}, {11, 12}, {17, 12}, {17, 3}, {21, 3}, {21, 9}, {24, 9}},
     1.0, 660, "#b98a3e", "#b08238", 3110, false,
     {{{-1, 0}, {8, 0.5}, {14, 7.5}, {24, 9}}, {{-1, 15}, {14, 15}, {19, 11}, {24, 9}}},
     {
      {0.82, {{Ant, 24, 0.33, 0}, {Butterfly, 9, 0.49, 3}, {Cricket, 6, 0.82, 6}}},
      {0.95, {{Beetle, 12, 0.82, 0}, {Scorpion, 3, 2.45, 4}, {Caterpillar, 4, 1.63, 6}}},
      {1.09, {{Wasp, 21, 0.29, 0}, {Mosquito, 3, 2.86, 4}, {Butterfly, 12, 0.49, 6}, {GoldenBeetle, 1, 1, 10}}},
      {1.22, {{Cricket, 12, 0.65, 0}, {Scorpion, 4, 2.04, 4}, {Caterpillar, 4, 1.63, 6}, {Butterfly, 12, 0.41, 10}, {Bat, 1, 1, 8}}},
      {1.36, {{Ant, 27, 0.29, 0}, {Beetle, 15, 0.65, 2}, {Mosquito, 3, 3.27, 6}, {Snail, 1, 6, 8}, {Caterpillar, 4, 1.63, 12}, {Butterfly, 12, 0.41, 14}, {Bat, 1, 1, 16}}}},
     24, 16, Season::Autumn},
    // 8: Frost Fen (25x17, winter)
    {"Frost Fen", "Winter arrives. The path doubles back on itself, so plants at the bends hit bugs twice.",
     {{-1, 2}, {10, 2}, {10, 6}, {5, 6}, {5, 11}, {15, 11}, {15, 4}, {20, 4}, {20, 14}, {25, 14}},
     1.0, 740, "#a9c3b8", "#a1bbb0", 1225, false,
     {{{-1, 8.5}, {8, 8.5}, {17, 1}, {25, 14}}, {{-1, 16}, {12, 16}, {18, 9}, {25, 14}}},
     {
      {0.77, {{Ant, 28, 0.3, 0}, {Butterfly, 10, 0.45, 3}, {Cricket, 7, 0.76, 6}}},
      {0.89, {{Beetle, 14, 0.76, 0}, {Scorpion, 4, 2.27, 4}, {Caterpillar, 5, 1.51, 6}}},
      {1.01, {{Wasp, 24, 0.26, 0}, {Mosquito, 4, 2.65, 4}, {Butterfly, 14, 0.45, 6}, {GoldenBeetle, 1, 1, 10}}},
      {1.12, {{Cricket, 14, 0.6, 0}, {Scorpion, 5, 1.89, 4}, {Caterpillar, 5, 1.51, 6}, {Butterfly, 14, 0.38, 10}}},
      {1.25, {{Ant, 32, 0.26, 0}, {Beetle, 18, 0.6, 2}, {Mosquito, 4, 3.02, 6}, {Snail, 2, 6, 8}, {Caterpillar, 5, 1.51, 12}, {Butterfly, 14, 0.38, 14}, {Bat, 1, 1, 16}}}},
     25, 17, Season::Winter},
    // 9: Snowdrift Hollow (27x18, winter)
    {"Snowdrift Hollow", "Deep snow, three sky lanes and two snails. Every bug you've met is here.",
     {{-1, 3}, {8, 3}, {8, 7}, {3, 7}, {3, 12}, {13, 12}, {13, 5}, {18, 5}, {18, 9}, {15, 9}, {15, 15}, {23, 15}, {23, 8}, {27, 8}},
     1.0, 860, "#dfe9ee", "#d6e1e7", 2412, false,
     {{{-1, 0}, {10, 0.5}, {20, 2}, {27, 8}}, {{-1, 17}, {10, 17}, {20, 11.5}, {27, 8}}, {{-1, 9.5}, {9, 9.5}, {17, 13}, {27, 8}}},
     {
      {0.70, {{Ant, 32, 0.28, 0}, {Butterfly, 12, 0.42, 3}, {Cricket, 8, 0.71, 6}}},
      {0.81, {{Beetle, 16, 0.71, 0}, {Scorpion, 4, 2.12, 4}, {Caterpillar, 6, 1.41, 6}}},
      {0.91, {{Wasp, 28, 0.25, 0}, {Mosquito, 4, 2.47, 4}, {Butterfly, 16, 0.42, 6}, {GoldenBeetle, 1, 1, 10}}},
      {1.01, {{Cricket, 16, 0.57, 0}, {Scorpion, 6, 1.77, 4}, {Caterpillar, 6, 1.41, 6}, {Butterfly, 16, 0.35, 10}, {Bat, 1, 1, 8}}},
      {1.12, {{Ant, 36, 0.25, 0}, {Beetle, 20, 0.57, 2}, {Mosquito, 4, 2.83, 6}, {Snail, 2, 6, 8}, {Caterpillar, 6, 1.41, 12}, {Butterfly, 16, 0.35, 14}, {Bat, 1, 1, 16}}}},
     27, 18, Season::Winter},
    // 10: Evergreen Summit (30x20, winter)
    {"Evergreen Summit", "The grand finale on the mountain top: the longest path, the biggest swarms and three snails. Guard the garden!",
     {{-1, 2}, {12, 2}, {12, 6}, {4, 6}, {4, 11}, {16, 11}, {16, 4}, {22, 4}, {22, 9}, {19, 9}, {19, 16}, {27, 16}, {27, 10}, {30, 10}},
     1.0, 920, "#c9dbe0", "#c0d3d8", 3030, false,
     {{{-1, 0}, {14, 0}, {25, 6}, {30, 10}}, {{-1, 19}, {14, 19}, {24, 13}, {30, 10}}, {{-1, 8.5}, {10, 8.5}, {18, 14}, {30, 10}}},
     {
      {1.31, {{Ant, 36, 0.27, 0}, {Butterfly, 14, 0.4, 3}, {Cricket, 9, 0.67, 6}}},
      {1.49, {{Beetle, 18, 0.67, 0}, {Scorpion, 4, 2, 4}, {Caterpillar, 7, 1.33, 6}}},
      {1.68, {{Wasp, 32, 0.23, 0}, {Mosquito, 4, 2.33, 4}, {Butterfly, 18, 0.4, 6}, {GoldenBeetle, 1, 1, 10}}},
      {1.86, {{Cricket, 18, 0.53, 0}, {Scorpion, 7, 1.67, 4}, {Caterpillar, 7, 1.33, 6}, {Butterfly, 18, 0.33, 10}, {Bat, 1, 1, 8}}},
      {2.05, {{Ant, 40, 0.23, 0}, {Beetle, 22, 0.53, 2}, {Mosquito, 4, 2.67, 6}, {Snail, 3, 6, 8}, {Caterpillar, 7, 1.33, 12}, {Butterfly, 18, 0.33, 14}, {Bat, 1, 1, 16}}}},
     30, 20, Season::Winter},
};

}  // namespace data
