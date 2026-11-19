// Garden Guard: game balance and map data (ported from js/data.js).
#pragma once

#include <QString>
#include <array>
#include <vector>

namespace data {

constexpr int COLS = 15;
constexpr int ROWS = 10;
constexpr int TILE = 64;
constexpr int W = COLS * TILE;   // 960 logical px
constexpr int H = ROWS * TILE;   // 640 logical px

constexpr double SELL_RATE = 0.7;      // refund = floor(spent * SELL_RATE)
constexpr int WAVE_BONUS_BASE = 20;    // coins on clearing wave n: BASE + PER * n
constexpr int WAVE_BONUS_PER = 5;
constexpr double MORTAR_FLIGHT = 0.7;  // seconds a mortar shell is in the air
constexpr double FLAME_HALF_ANGLE = 0.6;  // radians (~34°) each side of the chili's aim

constexpr int START_LIVES = 20;   // lives carry over from map to map

// A campaign map. The path is a list of tile waypoints joined by straight
// horizontal/vertical lines; the first and last are just off-grid (col -1 and
// col 15) so bugs walk in from the left and out at the house on the right.
struct MapDef {
    QString name;
    QString blurb;                              // shown before the map starts
    std::vector<std::array<int, 2>> path;
    double hpMult;                              // bug health multiplier on this map
    int startCoins;                             // coins reset to this when the map starts
    QString grassA, grassB;                     // checkerboard grass colors
    unsigned seed;                              // decoration layout
};

inline const std::vector<MapDef> MAPS = {
    {"Sunny Meadow", "A gentle meadow to get your garden growing.",
     {{-1, 1}, {2, 1}, {2, 6}, {6, 6}, {6, 2}, {10, 2}, {10, 8}, {13, 8}, {13, 4}, {15, 4}},
     1.0, 180, "#79c25a", "#73bb54", 20261118},
    {"Apple Orchard", "The bugs here are well fed: 40% more health.",
     {{-1, 8}, {3, 8}, {3, 1}, {7, 1}, {7, 7}, {11, 7}, {11, 2}, {15, 2}},
     1.4, 200, "#8cc35a", "#86bc52", 7051},
    {"Thorny Ridge", "Tough terrain and tougher bugs: 80% more health.",
     {{-1, 4}, {1, 4}, {1, 1}, {4, 1}, {4, 8}, {8, 8}, {8, 3}, {11, 3}, {11, 6}, {15, 6}},
     1.8, 230, "#6aae55", "#64a74f", 4242},
};

// How a plant attacks.
enum class Projectile {
    Bolt,     // homing pea
    Frost,    // homing, slows
    Mortar,   // lobbed shell, splash damage where it lands
    Spike,    // fast homing needle
    Spore,    // homing, poisons (damage over time, ignores armor)
    Flame,    // instant cone: hits every bug in front of it
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
};

enum EnemyType { Ant, Beetle, Wasp, Snail };

inline const std::vector<EnemyDef> ENEMIES = {
    {"Ant", QStringLiteral("🐜"), 32, 1.5, 5, 1, 0, 0, false, Facing::Left},
    {"Beetle", QStringLiteral("🐞"), 130, 0.9, 11, 1, 3, 0, false, Facing::Up},
    {"Wasp", QStringLiteral("🐝"), 45, 2.4, 7, 1, 0, 0.2, false, Facing::Left},
    {"Snail", QStringLiteral("🐌"), 1500, 0.45, 120, 5, 6, 0.5, true, Facing::Right},
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
    {1.65, {{Wasp, 12, 0.5, 0}, {Ant, 8, 0.8, 5}}},
    {1.90, {{Beetle, 8, 1.4, 0}, {Ant, 12, 0.6, 2}}},
    {2.20, {{Wasp, 22, 0.35, 0}}},
    {2.50, {{Beetle, 12, 1.1, 0}, {Ant, 14, 0.5, 3}, {Wasp, 10, 0.45, 9}}},
    {2.85, {{Ant, 18, 0.45, 0}, {Snail, 1, 1, 6}}},
    {3.25, {{Beetle, 16, 0.9, 0}, {Wasp, 22, 0.35, 4}}},
    {3.70, {{Ant, 24, 0.35, 0}, {Beetle, 18, 0.8, 2}, {Wasp, 26, 0.3, 8}, {Snail, 2, 6, 10}}},
};

}  // namespace data
