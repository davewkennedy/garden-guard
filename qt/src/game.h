// Garden Guard: simulation (enemies, towers, projectiles, waves, economy).
// Ported from js/game.js. No drawing or input here: the renderer reads the
// public state, the UI calls the methods, and everyone listens to the signals.
#pragma once

#include "data.h"

#include <QObject>
#include <QPointF>
#include <QString>
#include <vector>

// MapCleared: all waves on this map beaten, waiting for nextMap(). Won: whole campaign beaten.
enum class Phase { Build, Wave, MapCleared, Won, Lost };

// Campaign difficulty: scales bug HP and each map's start coins. Normal changes nothing.
enum class Difficulty { Easy, Normal, Hard };

struct Enemy {
    int id;
    data::EnemyType type;
    double x, y, angle;
    double hp, maxHp;
    double dist;        // px travelled along the path
    double slowUntil;   // state time when the slow ends
    double slowFactor;  // 0..1
    double hitTime;     // state time of the last hit, -1 if never
    double poisonDps = 0;
    double poisonUntil = 0;   // state time when the poison wears off
    bool alive = true;
    double abilityTimer = 0;  // counts down to the next hop / heal pulse
    double hopStart = -1;     // state time of the last hop, -1 if never (renderer arcs it)
    double healTime = -1;     // state time this bug was last healed, -1 if never
    int lane = -1;            // flyers: index into the map's sky lanes (-1 = walks the path)
    int route = 0;            // ground bugs: 0 = main path, 1 = second entrance (MapDef::path2)
    // Wild bats: free flight.
    double heading = 0;       // radians
    double spawnTime = 0;
    double startDist = 0;     // px to the house at spawn (dist = progress toward it)
    double trailTimer = 0;
    std::vector<QPointF> trail;   // recent positions (renderer draws a fading trail)
    // Grape vine: a lassoed flyer is grounded (every plant can hit it, it slows down).
    double groundedUntil = 0;
    int vineBy = -1;          // tower id holding the vine
    bool grounded(double now) const { return now < groundedUntil; }

    bool slowed(double now) const { return now < slowUntil && slowFactor > 0; }
    bool poisoned(double now) const { return now < poisonUntil && poisonDps > 0; }
};

struct Tower {
    int id;
    int type;           // index into data::TOWERS
    int level;          // 0-based index into levels
    int col, row;
    double x, y, angle;
    double cooldown;
    double shotTime;    // state time of the last shot, -1 if never
    int spent;          // total coins put into this tower
    // Optional fire point (ambush): only shoot bugs near this spot, aiming there while idle.
    bool hasFirePoint = false;
    double fireX = 0, fireY = 0;   // px
    int focusId = -1;              // focus fire: enemy id the player picked, -1 = none
    double stunnedUntil = 0;   // scorpion goo: state time when the plant can act again

    bool stunned(double now) const { return now < stunnedUntil; }
};

// Scorpion goo glob in flight toward a plant (an enemy projectile).
struct Spit {
    int id;
    int towerId;
    double fromX, fromY, toX, toY;
    double x, y;
    double progress;    // 0..1
    double stunTime;
};

// Tuned with a simtest sweep: 1.5 tiles / +30% is the first setting where ambushing
// with splash/flame plants pays off (smaller zones made plants hold fire too often).
constexpr double FIRE_POINT_RADIUS = 1.5 * data::TILE;   // px around a fire point that counts
constexpr double AMBUSH_BONUS = 1.3;                     // damage multiplier with a fire point

struct Shot {
    int id;
    data::Projectile kind;
    double x, y, fromX, fromY, toX, toY;
    double progress;    // 0..1
    double angle;
    // Internal flight data.
    int targetId;
    int damage;
    double speed;       // px/s (homing kinds)
    double slow, slowTime;
    double poisonDps, poisonTime;
    double splash;      // px (mortar)
    double startDist;
    int pierce = 0;              // thorn: bugs it can still hit
    std::vector<int> hitIds;     // thorn: bugs already hit
};

enum class EffectKind { Hit, Splash, Kill, Coin, Leak, Place, Slow, Poison, Flame,
                        Heal, Hop, Split, Jackpot, Goo, BossDust, FlyerFall, Lasso };

struct Effect {
    EffectKind kind;
    double x, y;
    double t, ttl;
    double radius;
    QString text;
    double angle = 0;   // flame: direction of the cone
};

class Game : public QObject {
    Q_OBJECT

public:
    explicit Game(QObject *parent = nullptr);

    // State (read by the renderer and UI; only Game changes it).
    Phase phase = Phase::Build;
    int mapIndex = 0;                  // current campaign map
    int totalMaps = int(data::MAPS.size());
    // Current map's grid in tiles, and the board size in logical px (cols/rows * TILE).
    int cols = data::DEFAULT_COLS, rows = data::DEFAULT_ROWS;
    double W = data::DEFAULT_COLS * data::TILE, H = data::DEFAULT_ROWS * data::TILE;
    int coins = 0;
    int lives = data::START_LIVES;
    int wave = 0;                      // waves started so far on this map
    int totalWaves = int(data::WAVES.size());
    double time = 0;                   // simulated seconds since reset
    std::vector<Enemy> enemies;
    std::vector<Tower> towers;
    std::vector<Shot> shots;
    std::vector<Effect> effects;
    std::vector<Spit> spits;           // scorpion goo in flight
    // Wind gusts (MapDef::windEvery). Time-based, so runs stay deterministic.
    double windStrength() const;   // 0..1 smooth envelope of the current gust
    double windWarning() const;    // 0..1 ramp during the warning second before a gust
    int windDir() const;           // +1 pushes down, -1 pushes up (alternates gust to gust)
    double windOffset() const { return windDir() * 1.5 * data::TILE * windStrength(); }
    // How far above its ground point a bug's sprite is drawn (flyers hover); shots aim there.
    double visualY(const Enemy &e) const { return data::ENEMIES[e.type].flying && !e.grounded(time) ? 16 : 0; }
    std::vector<std::vector<QPointF>> skyLanes;   // pixel polylines flyers follow
    std::vector<QPointF> path;         // pixel waypoints
    std::vector<QPointF> path2;        // second entrance's pixel waypoints (empty = one entrance)

    const data::MapDef &map() const { return data::MAPS[mapIndex]; }
    // This map's waves (falls back to the global list if the map has none).
    const std::vector<data::Wave> &waves() const { return map().waves.empty() ? data::WAVES : map().waves; }
    bool isLastMap() const { return mapIndex + 1 >= totalMaps; }
    // True when the player can't act: between maps, or the campaign is won/lost.
    bool isOver() const { return phase == Phase::MapCleared || phase == Phase::Won || phase == Phase::Lost; }
    bool isPathTile(int col, int row) const;
    bool canPlace(int col, int row) const;
    int terrainAt(int col, int row) const;   // data::TERRAIN_* kind, or -1 for plain grass/path
    Tower *towerAt(int col, int row);
    Tower *getTower(int id);

    // Plant unlocks (claude2-0c): a plant is available from its TowerDef::unlockMap on. Derived
    // from mapIndex, so reset(0) relocks and reset(N) unlocks everything up to map N.
    // Difficulty (Easy: bug HP x0.75, start coins x1.25; Hard: coins x0.9, bug HP x1.0 on
    // maps 1-2 then x1.15 from map 3, so there's no early wall).
    // Applied when bugs spawn and when a map loads; Normal is exactly the tuned game.
    Difficulty difficulty = Difficulty::Normal;
    void setDifficulty(Difficulty d);  // also re-applies start coins if nothing is planted yet
    double difficultyHpMult() const;
    double difficultyCoinMult() const;
    int startCoins() const;            // this map's start coins at the current difficulty

    bool unlockAll = false;            // test/debug override: every plant available
    bool isUnlocked(int type) const {
        return type >= 0 && type < int(data::TOWERS.size()) && (unlockAll || data::TOWERS[type].unlockMap <= mapIndex);
    }

    Tower *placeTower(int type, int col, int row);   // nullptr if locked, unaffordable or blocked
    int upgradeCost(const Tower &t) const;   // -1 when at max level
    bool upgradeTower(int id);
    int sellValue(const Tower &t) const;
    int sellTower(int id);                   // refund, 0 if not found

    // Fire points: the point must be within the plant's range. Returns false if not.
    bool setFirePoint(int towerId, double x, double y);
    void clearFirePoint(int towerId);
    int damageOf(const Tower &t) const;   // includes the ambush bonus
    // Focus fire: these plants shoot this bug whenever it's in range, until it's gone.
    bool focusFire(const std::vector<int> &towerIds, int enemyId);
    void clearFocus(int towerId);
    int enemyNear(double x, double y, double radius) const;   // enemy id near a point, -1 if none
    // Targeting (also used by the renderer/UI to show what a plant would shoot).
    bool canHit(const Tower &t, const Enemy &e) const;   // claude2-63: flyers vs ground attacks
    bool eligible(const Tower &t, const Enemy &e, double rangePx) const;
    const Enemy *pickTarget(const Tower &t, double rangePx) const;

    bool startWave();
    void update(double dt);
    bool nextMap();     // after MapCleared: next map, coins and plants reset, lives kept
    void reset(int startMap = 0);   // fresh campaign (full lives), optionally from a later map

signals:
    void shot(int towerType);
    void killed(int reward);
    void leaked(int livesLost);
    void placed(int towerId);
    void upgraded(int towerId);
    void sold(int refund);
    void waveStarted(int wave);
    void waveEnded(int wave, int bonus, int harvest);   // harvest = sunflower income
    void mapCleared(int mapIndex);
    void mapStarted(int mapIndex);
    void won();
    void lost();
    void bugSound(const QString &name);
    void wildcard(int enemyId);           // a wild bat just appeared (UI toast)
    void gust();                          // a wind gust is 1 s away (UI toast)   // bug_hop, bug_heal, bug_split, jackpot

private:
    struct Spawner {
        data::EnemyType type;
        int remaining;
        double next;
        double gap;
        double hpMult;
    };

    void loadMap(int index);   // path, path tiles, coins; clears towers and bugs
    void pointAt(double d, double &x, double &y, double &angle) const;
    void addEffect(EffectKind kind, double x, double y, double radius = -1,
                   const QString &text = {}, double angle = 0);
    Enemy *findEnemy(int id);
    void spawnEnemy(data::EnemyType type, double hpMult);
    void updateSpawns(double dt);
    bool spawningDone() const;
    void updateEnemies(double dt);
    void updatePoison(double dt);
    bool damageEnemy(Enemy &e, double amount);   // true if it survived
    void killEnemy(Enemy &e);
    void updateBugAbilities(double dt);          // cricket hops, mosquito heals
    void flushPendingBugs();                     // adds bugs born mid-update (grubs)
    void placeBug(Enemy &e);                     // x/y/angle from dist on its lane or the path
    double routeLength(const Enemy &e) const;    // px length of its lane or the path
    void updateSpits(double dt);
    bool steerWildBat(Enemy &e, double dt, double mult);   // true when it reached the house
    void fireVine(Tower &t, double dt);                 // scorpion goo flight + stun on impact
    struct Route { std::vector<double> segLen, segStart; double length = 0; };
    std::vector<Route> laneRoutes_;
    int nextLane_ = 0;
    Route route2_;                               // second entrance (path2) segments, if any
    int nextRoute_ = 0;                          // round robin of ground spawns over the entrances
    struct PendingBug { data::EnemyType type; double maxHp; double dist; int route = 0; };
    std::vector<PendingBug> pendingBugs_;
    void applySlow(Enemy &e, double slow, double slowTime);
    void applyPoison(Enemy &e, double dps, double time);
    void updateTowers(double dt);
    void fire(Tower &t, const Enemy &target);
    void fireFlame(Tower &t);
    void fireThorn(Tower &t);
    void fireKernels(Tower &t);
    double rateBoost(const Tower &t) const;      // clover aura multiplier, >= 1
    bool stepThorn(Shot &p, double dt);
    int harvest();                               // pays out sunflower income
    void updateShots(double dt);
    bool stepHoming(Shot &p, double dt);         // true when finished
    bool stepMortar(Shot &p, double dt);
    void updateEffects(double dt);

    std::vector<double> segLen_, segStart_;
    double pathLength_ = 0;
    double prevWindOffset_ = 0;                  // last frame's gust offset (wild bats move by the change)
    std::vector<int> terrain_;                   // cols * rows, -1 = none
    std::vector<bool> pathTiles_;                // COLS * ROWS
    double leakX_ = 0, leakY_ = 0;
    std::vector<Spawner> spawners_;
    double waveTime_ = 0;
    int nextId_ = 1;
};
