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
};

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
};

enum class EffectKind { Hit, Splash, Kill, Coin, Leak, Place, Slow, Poison, Flame };

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
    int coins = 0;
    int lives = data::START_LIVES;
    int wave = 0;                      // waves started so far on this map
    int totalWaves = int(data::WAVES.size());
    double time = 0;                   // simulated seconds since reset
    std::vector<Enemy> enemies;
    std::vector<Tower> towers;
    std::vector<Shot> shots;
    std::vector<Effect> effects;
    std::vector<QPointF> path;         // pixel waypoints

    const data::MapDef &map() const { return data::MAPS[mapIndex]; }
    bool isLastMap() const { return mapIndex + 1 >= totalMaps; }
    // True when the player can't act: between maps, or the campaign is won/lost.
    bool isOver() const { return phase == Phase::MapCleared || phase == Phase::Won || phase == Phase::Lost; }
    bool isPathTile(int col, int row) const;
    bool canPlace(int col, int row) const;
    Tower *towerAt(int col, int row);
    Tower *getTower(int id);

    Tower *placeTower(int type, int col, int row);
    int upgradeCost(const Tower &t) const;   // -1 when at max level
    bool upgradeTower(int id);
    int sellValue(const Tower &t) const;
    int sellTower(int id);                   // refund, 0 if not found

    bool startWave();
    void update(double dt);
    bool nextMap();     // after MapCleared: next map, coins and plants reset, lives kept
    void reset();       // back to the first map with a fresh campaign

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
    void applySlow(Enemy &e, double slow, double slowTime);
    void applyPoison(Enemy &e, double dps, double time);
    void updateTowers(double dt);
    void fire(Tower &t, const Enemy &target);
    void fireFlame(Tower &t);
    int harvest();                               // pays out sunflower income
    void updateShots(double dt);
    bool stepHoming(Shot &p, double dt);         // true when finished
    bool stepMortar(Shot &p, double dt);
    void updateEffects(double dt);

    std::vector<double> segLen_, segStart_;
    double pathLength_ = 0;
    std::vector<bool> pathTiles_;                // COLS * ROWS
    double leakX_ = 0, leakY_ = 0;
    std::vector<Spawner> spawners_;
    double waveTime_ = 0;
    int nextId_ = 1;
};
