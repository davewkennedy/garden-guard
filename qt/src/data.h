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
     "Long range, heavy hits.", "#3d8b3d",
     {{.cost = 125, .damage = 70, .range = 5.0, .rate = 0.5},
      {.cost = 120, .damage = 120, .range = 5.5, .rate = 0.55},
      {.cost = 180, .damage = 200, .range = 6.0, .rate = 0.6}}},
    {"mushroom", "Spore Mushroom", QStringLiteral("🍄"), Projectile::Spore, 6,
     "Poisons bugs. Ignores armor.", "#b06ad9",
     {{.cost = 90, .damage = 4, .range = 2.6, .rate = 0.9, .poisonDps = 9, .poisonTime = 3},
      {.cost = 90, .damage = 6, .range = 2.8, .rate = 1.0, .poisonDps = 16, .poisonTime = 3.5},
      {.cost = 140, .damage = 10, .range = 3.0, .rate = 1.1, .poisonDps = 28, .poisonTime = 4}}},
    {"chili", "Chili Pepper", QStringLiteral("🌶️"), Projectile::Flame, 0,
     "Burns every bug in a cone.", "#e5402b",
     {{.cost = 110, .damage = 14, .range = 1.7, .rate = 1.2},
      {.cost = 110, .damage = 22, .range = 1.9, .rate = 1.3},
      {.cost = 170, .damage = 36, .range = 2.1, .rate = 1.5}}},
    {"sunflower", "Sunflower", QStringLiteral("🌻"), Projectile::None, 0,
     "Earns coins every wave.", "#f2b705",
     {{.cost = 75, .income = 15},
      {.cost = 80, .income = 28},
      {.cost = 120, .income = 45}}},
    {"rose", "Thorn Rose", QStringLiteral("🌹"), Projectile::Thorn, 11,
     "Thorns pierce a line of bugs.", "#d6336c",
     {{.cost = 95, .damage = 16, .range = 3.0, .rate = 0.9, .pierce = 3},
      {.cost = 95, .damage = 26, .range = 3.3, .rate = 1.0, .pierce = 4},
      {.cost = 150, .damage = 42, .range = 3.6, .rate = 1.1, .pierce = 6}}},
    {"corn", "Corn Popper", QStringLiteral("🌽"), Projectile::Kernel, 10,
     "Pops kernels at several bugs.", "#f5c518",
     {{.cost = 85, .damage = 9, .range = 2.5, .rate = 0.9, .shots = 3},
      {.cost = 90, .damage = 14, .range = 2.7, .rate = 1.0, .shots = 4},
      {.cost = 140, .damage = 22, .range = 2.9, .rate = 1.1, .shots = 5}}},
    {"clover", "Lucky Clover", QStringLiteral("🍀"), Projectile::Aura, 0,
     "Nearby plants fire faster.", "#2fb36b",
     {{.cost = 100, .range = 1.6, .boost = 0.2},
      {.cost = 100, .range = 1.8, .boost = 0.3},
      {.cost = 150, .range = 2.1, .boost = 0.45}}},
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
    {"Bat", QStringLiteral("🦇"), 150, 1.3, 12, 1, 1, 0.2, false, Facing::Up,
     1.0, -1, 0, 0, 0, 0, 0, 0, false, true, "Tough night flyer. Mortars and chilies can't reach it."},
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
};

inline const std::vector<MapDef> MAPS = {
    // 1: the basics. Ground bugs only.
    {"Sunny Meadow", "A gentle meadow to get your garden growing.",
     {{-1, 1}, {2, 1}, {2, 6}, {6, 6}, {6, 2}, {10, 2}, {10, 8}, {13, 8}, {13, 4}, {15, 4}},
     1.0, 180, "#79c25a", "#73bb54", 20261118, false, {},
     {{1.00, {{Ant, 10, 0.9, 0}}},
      {1.20, {{Ant, 14, 0.7, 0}, {Beetle, 2, 2.5, 6}}},
      {1.40, {{Wasp, 10, 0.5, 0}, {Ant, 8, 0.7, 5}}},
      {1.70, {{Beetle, 6, 1.4, 0}, {Ant, 10, 0.6, 2}, {Wasp, 6, 0.5, 8}}},
      {2.00, {{Ant, 20, 0.3, 0}, {Wasp, 14, 0.35, 3}, {Beetle, 6, 1.0, 6}}}}},   // mixed rush
    // 2: first flyers (butterflies on a sky lane) and splitting caterpillars.
    {"Apple Orchard", "Butterflies flutter along the sky lane, out of reach of mortars and chilis. Caterpillars split when squashed!",
     {{-1, 8}, {3, 8}, {3, 1}, {7, 1}, {7, 7}, {11, 7}, {11, 2}, {15, 2}},
     1.0, 320, "#8cc35a", "#86bc52", 7051, false,
     {{{-1, 9}, {5, 9}, {9, 4}, {15, 2}}},
     {{0.84, {{Ant, 12, 0.7, 0}, {Caterpillar, 2, 3.0, 5}}},
      {1.05, {{Butterfly, 8, 0.8, 0}, {Ant, 10, 0.6, 4}}},
      {1.26, {{Beetle, 6, 1.4, 0}, {Caterpillar, 4, 2.0, 3}, {Butterfly, 6, 0.7, 8}}},
      {1.47, {{Wasp, 14, 0.4, 0}, {Butterfly, 10, 0.6, 4}, {Caterpillar, 3, 2.5, 8}}},
      {1.68, {{Ant, 16, 0.4, 0}, {Beetle, 8, 1.0, 2}, {Butterfly, 14, 0.5, 6}, {Caterpillar, 4, 2.0, 10}}}}},
    // 3: crickets hop, scorpions stun plants; first snail boss.
    {"Thorny Ridge", "Crickets leap ahead and scorpions spit goo that stuns plants: don't crowd the path. A snail guards the ridge.",
     {{-1, 4}, {1, 4}, {1, 1}, {4, 1}, {4, 8}, {8, 8}, {8, 3}, {11, 3}, {11, 6}, {15, 6}},
     1.0, 360, "#6aae55", "#64a74f", 4242, false,
     {{{-1, 0}, {6, 0.5}, {10, 5}, {15, 6}}},
     {{0.84, {{Cricket, 6, 1.2, 0}, {Ant, 12, 0.6, 3}}},
      {1.02, {{Scorpion, 3, 3.0, 0}, {Beetle, 6, 1.3, 2}, {Butterfly, 6, 0.8, 6}}},
      {1.20, {{Cricket, 8, 1.0, 0}, {Wasp, 12, 0.4, 4}, {Caterpillar, 3, 2.5, 8}}},
      {1.44, {{Scorpion, 5, 2.5, 0}, {Butterfly, 10, 0.6, 3}, {Cricket, 6, 1.0, 8}}},
      {1.68, {{Ant, 18, 0.4, 0}, {Beetle, 10, 0.9, 2}, {Snail, 1, 1, 6}, {Scorpion, 3, 3.0, 9},
              {Butterfly, 10, 0.6, 10}}}}},
    // 4: night. Bats on two sky lanes, mosquito healers, a golden beetle cameo.
    {"Moonlit Garden", "Night falls. Bats patrol two sky lanes and mosquitoes heal the swarm. Tip: a Frost Fern where a lane crosses the path slows bugs on the ground and in the air. Watch for a golden glint!",
     {{-1, 1}, {12, 1}, {12, 4}, {2, 4}, {2, 8}, {15, 8}},
     1.0, 500, "#2f5f42", "#2b583d", 9001, true,
     {{{-1, 2.5}, {8, 2.5}, {14, 6}, {15, 8}}, {{-1, 6.5}, {9, 6.2}, {15, 8}}},
     {{0.80, {{Bat, 4, 2.0, 0}, {Ant, 14, 0.5, 2}}},
      {0.95, {{Mosquito, 2, 4.0, 0}, {Beetle, 8, 1.2, 0}, {Butterfly, 8, 0.6, 5}}},
      {1.15, {{Bat, 6, 1.6, 0}, {Cricket, 8, 1.0, 3}, {GoldenBeetle, 1, 1, 9}}},
      {1.35, {{Scorpion, 4, 2.5, 0}, {Mosquito, 2, 3.5, 3}, {Bat, 4, 1.8, 6}, {Caterpillar, 3, 2.0, 8}}},
      {1.55, {{Beetle, 10, 0.8, 0}, {Bat, 5, 1.5, 3}, {Mosquito, 2, 4.0, 5}, {Wasp, 12, 0.35, 8},
              {Snail, 1, 1, 10}}}}},
    // 5: the finale. Everything at once, two snails at the end.
    {"Harvest Hollow", "The autumn finale: every bug in the garden, two sky lanes, and two snails at the end. Good luck!",
     {{-1, 6}, {3, 6}, {3, 2}, {7, 2}, {7, 7}, {10, 7}, {10, 1}, {13, 1}, {13, 5}, {15, 5}},
     1.0, 500, "#c9a24a", "#c29a42", 1031, false,
     {{{-1, 0}, {8, 0.3}, {11.5, 3}, {15, 5}}, {{-1, 9}, {6, 8.5}, {12, 6}, {15, 5}}},
     {{0.86, {{Ant, 16, 0.4, 0}, {Butterfly, 8, 0.6, 3}, {Cricket, 6, 1.0, 6}}},
      {1.04, {{Beetle, 10, 1.0, 0}, {Scorpion, 3, 3.0, 4}, {Bat, 4, 2.0, 6}}},
      {1.22, {{Caterpillar, 6, 1.6, 0}, {Mosquito, 3, 3.5, 4}, {Wasp, 14, 0.35, 6}, {GoldenBeetle, 1, 1, 10}}},
      {1.31, {{Cricket, 10, 0.8, 0}, {Bat, 5, 1.5, 3}, {Scorpion, 3, 2.5, 6}, {Butterfly, 8, 0.6, 10}}},
      {1.40, {{Ant, 20, 0.35, 0}, {Beetle, 12, 0.8, 2}, {Bat, 5, 1.5, 5}, {Mosquito, 2, 4.0, 6},
              {Snail, 2, 6, 8}, {Caterpillar, 3, 2.0, 12}, {Butterfly, 8, 0.6, 14}}}}},
};

}  // namespace data
