#include "game.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double MAX_DT = 0.05;
constexpr size_t MAX_EFFECTS = 400;
constexpr double TILE = data::TILE;

double effectTtl(EffectKind k) {
    switch (k) {
    case EffectKind::Hit: return 0.25;
    case EffectKind::Splash: return 0.4;
    case EffectKind::Kill: return 0.45;
    case EffectKind::Coin: return 0.9;
    case EffectKind::Leak: return 0.8;
    case EffectKind::Place: return 0.5;
    case EffectKind::Slow: return 0.5;
    case EffectKind::Poison: return 0.6;
    case EffectKind::Flame: return 0.3;
    }
    return 0.5;
}

double effectRadius(EffectKind k) {
    switch (k) {
    case EffectKind::Hit: return 10;
    case EffectKind::Kill: return 18;
    case EffectKind::Leak: return 28;
    case EffectKind::Place: return 32;
    case EffectKind::Slow: return 14;
    case EffectKind::Poison: return 14;
    default: return 0;
    }
}

int sign(int v) { return (v > 0) - (v < 0); }

template <typename T, typename Pred>
void removeIf(std::vector<T> &v, Pred pred) {
    v.erase(std::remove_if(v.begin(), v.end(), pred), v.end());
}

}  // namespace

Game::Game(QObject *parent) : QObject(parent) {
    loadMap(0);
}

void Game::loadMap(int index) {
    mapIndex = index;
    const auto &tiles = map().path;

    // Fresh start on this map: coins reset, no plants or bugs. Lives are kept.
    phase = Phase::Build;
    coins = map().startCoins;
    wave = 0;
    time = 0;
    enemies.clear();
    towers.clear();
    shots.clear();
    effects.clear();
    spawners_.clear();
    waveTime_ = 0;

    // Path in pixels, with segment lengths for walking along it by distance.
    path.clear();
    segLen_.clear();
    segStart_.clear();
    pathLength_ = 0;
    for (const auto &p : tiles)
        path.emplace_back(p[0] * TILE + TILE / 2, p[1] * TILE + TILE / 2);
    for (size_t i = 0; i + 1 < path.size(); i++) {
        double L = std::hypot(path[i + 1].x() - path[i].x(), path[i + 1].y() - path[i].y());
        segStart_.push_back(pathLength_);
        segLen_.push_back(L);
        pathLength_ += L;
    }

    // Path tiles: every in-grid tile a straight segment passes through.
    pathTiles_.assign(data::COLS * data::ROWS, false);
    for (size_t i = 0; i + 1 < tiles.size(); i++) {
        int c0 = tiles[i][0], r0 = tiles[i][1];
        int c1 = tiles[i + 1][0], r1 = tiles[i + 1][1];
        int steps = std::max(std::abs(c1 - c0), std::abs(r1 - r0));
        for (int s = 0; s <= steps; s++) {
            int c = c0 + sign(c1 - c0) * s;
            int r = r0 + sign(r1 - r0) * s;
            if (c >= 0 && c < data::COLS && r >= 0 && r < data::ROWS)
                pathTiles_[r * data::COLS + c] = true;
        }
    }

    // Leak effects appear at the path end, pulled onto the visible board.
    const QPointF &last = path.back();
    leakX_ = std::clamp(last.x(), TILE / 2, data::W - TILE / 2);
    leakY_ = std::clamp(last.y(), TILE / 2, data::H - TILE / 2);
}

// ---------- Path ----------

void Game::pointAt(double d, double &x, double &y, double &angle) const {
    if (d < 0) d = 0;
    size_t i = 0;
    while (i < segLen_.size() - 1 && d > segStart_[i] + segLen_[i]) i++;
    const QPointF &a = path[i];
    const QPointF &b = path[i + 1];
    double L = segLen_[i] > 0 ? segLen_[i] : 1;
    double t = std::clamp((d - segStart_[i]) / L, 0.0, 1.0);
    x = a.x() + (b.x() - a.x()) * t;
    y = a.y() + (b.y() - a.y()) * t;
    angle = std::atan2(b.y() - a.y(), b.x() - a.x());
}

void Game::addEffect(EffectKind kind, double x, double y, double radius, const QString &text,
                     double angle) {
    if (effects.size() >= MAX_EFFECTS) effects.erase(effects.begin());
    effects.push_back({kind, x, y, 0, effectTtl(kind), radius >= 0 ? radius : effectRadius(kind),
                       text, angle});
}

// ---------- Grid & towers ----------

bool Game::isPathTile(int col, int row) const {
    if (col < 0 || col >= data::COLS || row < 0 || row >= data::ROWS) return false;
    return pathTiles_[row * data::COLS + col];
}

bool Game::canPlace(int col, int row) const {
    if (col < 0 || col >= data::COLS || row < 0 || row >= data::ROWS) return false;
    if (isPathTile(col, row)) return false;
    return std::none_of(towers.begin(), towers.end(),
                        [&](const Tower &t) { return t.col == col && t.row == row; });
}

Tower *Game::towerAt(int col, int row) {
    for (auto &t : towers)
        if (t.col == col && t.row == row) return &t;
    return nullptr;
}

Tower *Game::getTower(int id) {
    for (auto &t : towers)
        if (t.id == id) return &t;
    return nullptr;
}

Tower *Game::placeTower(int type, int col, int row) {
    if (type < 0 || type >= int(data::TOWERS.size()) || isOver() || !canPlace(col, row)) return nullptr;
    int cost = data::TOWERS[type].levels[0].cost;
    if (coins < cost) return nullptr;
    coins -= cost;
    Tower t{nextId_++, type, 0, col, row,
            col * TILE + TILE / 2, row * TILE + TILE / 2, -M_PI / 2,
            0, -1, cost};
    towers.push_back(t);
    addEffect(EffectKind::Place, t.x, t.y, TILE / 2);
    emit placed(t.id);
    return &towers.back();
}

int Game::upgradeCost(const Tower &t) const {
    const auto &levels = data::TOWERS[t.type].levels;
    return t.level + 1 < int(levels.size()) ? levels[t.level + 1].cost : -1;
}

bool Game::upgradeTower(int id) {
    Tower *t = getTower(id);
    if (!t || isOver()) return false;
    int cost = upgradeCost(*t);
    if (cost < 0 || coins < cost) return false;
    coins -= cost;
    t->level++;
    t->spent += cost;
    emit upgraded(t->id);
    return true;
}

int Game::sellValue(const Tower &t) const {
    return int(std::floor(t.spent * data::SELL_RATE));
}

int Game::sellTower(int id) {
    Tower *t = getTower(id);
    if (!t || isOver()) return 0;
    int refund = sellValue(*t);
    coins += refund;
    removeIf(towers, [id](const Tower &x) { return x.id == id; });
    emit sold(refund);
    return refund;
}

// ---------- Waves ----------

bool Game::startWave() {
    if (phase != Phase::Build || wave >= totalWaves) return false;
    const data::Wave &w = data::WAVES[wave];
    wave++;
    phase = Phase::Wave;
    waveTime_ = 0;
    spawners_.clear();
    for (const auto &g : w.groups)
        spawners_.push_back({g.enemy, g.count, g.delay, g.gap, w.hpMult});
    emit waveStarted(wave);
    return true;
}

void Game::spawnEnemy(data::EnemyType type, double hpMult) {
    const data::EnemyDef &def = data::ENEMIES[type];
    Enemy e{};
    e.id = nextId_++;
    e.type = type;
    pointAt(0, e.x, e.y, e.angle);
    e.hp = e.maxHp = def.hp * hpMult * map().hpMult;
    e.dist = 0;
    e.slowUntil = 0;
    e.slowFactor = 0;
    e.hitTime = -1;
    enemies.push_back(e);
}

void Game::updateSpawns(double dt) {
    waveTime_ += dt;
    for (auto &s : spawners_) {
        while (s.remaining > 0 && waveTime_ >= s.next) {
            spawnEnemy(s.type, s.hpMult);
            s.remaining--;
            s.next += s.gap;
        }
    }
}

bool Game::spawningDone() const {
    return std::all_of(spawners_.begin(), spawners_.end(),
                       [](const Spawner &s) { return s.remaining <= 0; });
}

// ---------- Enemies ----------

Enemy *Game::findEnemy(int id) {
    for (auto &e : enemies)
        if (e.id == id && e.alive) return &e;
    return nullptr;
}

void Game::updateEnemies(double dt) {
    for (auto &e : enemies) {
        if (!e.alive) continue;
        const data::EnemyDef &def = data::ENEMIES[e.type];
        double mult = 1;
        if (time < e.slowUntil) mult = 1 - e.slowFactor;
        else e.slowFactor = 0;
        e.dist += def.speed * TILE * mult * dt;

        if (e.dist >= pathLength_) {
            e.alive = false;
            if (phase == Phase::Lost) continue;
            lives -= def.lives;
            addEffect(EffectKind::Leak, leakX_, leakY_);
            emit leaked(def.lives);
            if (lives <= 0) {
                lives = 0;
                phase = Phase::Lost;
                emit lost();
            }
            continue;
        }
        pointAt(e.dist, e.x, e.y, e.angle);
    }
    removeIf(enemies, [](const Enemy &e) { return !e.alive; });
}

bool Game::damageEnemy(Enemy &e, double amount) {
    if (!e.alive) return false;
    const data::EnemyDef &def = data::ENEMIES[e.type];
    double dmg = std::max(1.0, amount - def.armor);
    e.hp -= dmg;
    e.hitTime = time;
    addEffect(EffectKind::Hit, e.x, e.y);
    if (e.hp <= 0) {
        killEnemy(e);
        return false;
    }
    return true;
}

void Game::killEnemy(Enemy &e) {
    const data::EnemyDef &def = data::ENEMIES[e.type];
    e.hp = 0;
    e.alive = false;
    coins += def.reward;
    addEffect(EffectKind::Kill, e.x, e.y);
    addEffect(EffectKind::Coin, e.x, e.y, 0, QStringLiteral("+%1").arg(def.reward));
    emit killed(def.reward);
}

// Poison ticks every update and ignores armor, so it's strong against beetles and snails.
void Game::updatePoison(double dt) {
    for (auto &e : enemies) {
        if (!e.alive || !e.poisoned(time)) continue;
        e.hp -= e.poisonDps * dt;
        if (e.hp <= 0) killEnemy(e);
    }
}

void Game::applyPoison(Enemy &e, double dps, double poisonTime) {
    if (!e.alive || dps <= 0) return;
    // A stronger poison replaces a weaker one; either way the timer refreshes.
    double active = e.poisoned(time) ? e.poisonDps : 0;
    e.poisonDps = std::max(active, dps);
    e.poisonUntil = time + poisonTime;
    addEffect(EffectKind::Poison, e.x, e.y);
}

void Game::applySlow(Enemy &e, double slow, double slowTime) {
    if (!e.alive || slow <= 0) return;
    const data::EnemyDef &def = data::ENEMIES[e.type];
    double active = time < e.slowUntil ? e.slowFactor : 0;
    e.slowFactor = std::max(active, slow * (1 - def.slowResist));
    e.slowUntil = time + slowTime;
    addEffect(EffectKind::Slow, e.x, e.y);
}

// ---------- Towers ----------

void Game::updateTowers(double dt) {
    for (auto &t : towers) {
        const data::TowerDef &def = data::TOWERS[t.type];
        if (def.projectile == data::Projectile::None) continue;   // sunflowers don't attack
        const data::TowerLevel &lv = def.levels[t.level];
        t.cooldown = std::max(0.0, t.cooldown - dt);

        // Target "first": in range with the largest distance travelled.
        double r2 = std::pow(lv.range * TILE, 2);
        const Enemy *target = nullptr;
        for (const auto &e : enemies) {
            if (!e.alive) continue;
            double dx = e.x - t.x, dy = e.y - t.y;
            if (dx * dx + dy * dy <= r2 && (!target || e.dist > target->dist)) target = &e;
        }
        if (!target) continue;
        t.angle = std::atan2(target->y - t.y, target->x - t.x);
        if (t.cooldown > 0) continue;
        if (def.projectile == data::Projectile::Flame) fireFlame(t);
        else fire(t, *target);
    }
}

// Chili: instantly burns every bug within range inside a cone around its aim.
void Game::fireFlame(Tower &t) {
    const data::TowerLevel &lv = data::TOWERS[t.type].levels[t.level];
    const double range = lv.range * TILE;
    t.shotTime = time;
    t.cooldown = 1 / lv.rate;
    addEffect(EffectKind::Flame, t.x, t.y, range, {}, t.angle);
    for (auto &e : enemies) {
        if (!e.alive) continue;
        double dx = e.x - t.x, dy = e.y - t.y;
        if (dx * dx + dy * dy > range * range) continue;
        double off = std::remainder(std::atan2(dy, dx) - t.angle, 2 * M_PI);   // -pi..pi
        if (std::abs(off) <= data::FLAME_HALF_ANGLE) damageEnemy(e, lv.damage);
    }
    emit shot(t.type);
}

int Game::harvest() {
    int total = 0;
    for (const auto &t : towers) {
        int income = data::TOWERS[t.type].levels[t.level].income;
        if (income <= 0) continue;
        total += income;
        addEffect(EffectKind::Coin, t.x, t.y - 8, 0, QStringLiteral("+%1").arg(income));
    }
    coins += total;
    return total;
}

void Game::fire(Tower &t, const Enemy &target) {
    const data::TowerDef &def = data::TOWERS[t.type];
    const data::TowerLevel &lv = def.levels[t.level];
    t.shotTime = time;
    t.cooldown = 1 / lv.rate;
    Shot p{};
    p.id = nextId_++;
    p.kind = def.projectile;
    p.x = p.fromX = t.x;
    p.y = p.fromY = t.y;
    p.toX = target.x;
    p.toY = target.y;
    p.progress = 0;
    p.angle = t.angle;
    p.targetId = target.id;
    p.damage = lv.damage;
    p.speed = def.projectileSpeed * TILE;
    p.slow = lv.slow;
    p.slowTime = lv.slowTime;
    p.poisonDps = lv.poisonDps;
    p.poisonTime = lv.poisonTime;
    p.splash = lv.splash * TILE;
    p.startDist = std::hypot(target.x - t.x, target.y - t.y);
    if (p.startDist <= 0) p.startDist = 1;
    shots.push_back(p);
    emit shot(t.type);
}

// ---------- Projectiles ----------

void Game::updateShots(double dt) {
    std::vector<int> done;
    for (auto &p : shots) {
        bool finished = p.kind == data::Projectile::Mortar ? stepMortar(p, dt) : stepHoming(p, dt);
        if (finished) done.push_back(p.id);
    }
    if (!done.empty())
        removeIf(shots, [&](const Shot &p) {
            return std::find(done.begin(), done.end(), p.id) != done.end();
        });
}

bool Game::stepHoming(Shot &p, double dt) {
    Enemy *target = findEnemy(p.targetId);
    if (target) { p.toX = target->x; p.toY = target->y; }
    double dx = p.toX - p.x, dy = p.toY - p.y;
    double d = std::hypot(dx, dy);
    double step = p.speed * dt;
    if (d > 0) p.angle = std::atan2(dy, dx);
    if (d <= step || p.speed <= 0) {
        p.x = p.toX;
        p.y = p.toY;
        p.progress = 1;
        // If the target died before impact, the shot fizzles with no damage.
        if (target && damageEnemy(*target, p.damage)) {
            if (p.slow > 0) applySlow(*target, p.slow, p.slowTime);
            if (p.poisonDps > 0) applyPoison(*target, p.poisonDps, p.poisonTime);
        }
        return true;
    }
    p.x += dx / d * step;
    p.y += dy / d * step;
    p.progress = std::clamp(1 - (d - step) / std::max(p.startDist, d), 0.0, 1.0);
    return false;
}

bool Game::stepMortar(Shot &p, double dt) {
    p.progress = std::min(1.0, p.progress + dt / data::MORTAR_FLIGHT);
    p.x = p.fromX + (p.toX - p.fromX) * p.progress;
    p.y = p.fromY + (p.toY - p.fromY) * p.progress;
    p.angle = std::atan2(p.toY - p.fromY, p.toX - p.fromX);
    if (p.progress < 1) return false;
    addEffect(EffectKind::Splash, p.toX, p.toY, p.splash);
    double r2 = p.splash * p.splash;
    for (auto &e : enemies) {
        if (!e.alive) continue;
        double dx = e.x - p.toX, dy = e.y - p.toY;
        if (dx * dx + dy * dy <= r2) damageEnemy(e, p.damage);
    }
    return true;
}

// ---------- Effects ----------

void Game::updateEffects(double dt) {
    for (auto &fx : effects) fx.t += dt;
    removeIf(effects, [](const Effect &fx) { return fx.t >= fx.ttl; });
}

// ---------- Main step ----------

void Game::update(double dt) {
    if (isOver()) return;
    dt = std::min(MAX_DT, dt);
    if (!(dt > 0)) return;

    time += dt;
    if (phase == Phase::Wave) updateSpawns(dt);

    updateEnemies(dt);
    if (phase == Phase::Lost) { updateEffects(dt); return; }

    updatePoison(dt);
    updateTowers(dt);
    updateShots(dt);
    removeIf(enemies, [](const Enemy &e) { return !e.alive; });
    updateEffects(dt);

    if (phase == Phase::Wave && spawningDone() && enemies.empty()) {
        int bonus = data::WAVE_BONUS_BASE + data::WAVE_BONUS_PER * wave;
        coins += bonus;
        int harvested = harvest();
        bool lastWave = wave >= totalWaves;
        phase = !lastWave ? Phase::Build : isLastMap() ? Phase::Won : Phase::MapCleared;
        spawners_.clear();
        emit waveEnded(wave, bonus, harvested);
        if (phase == Phase::MapCleared) emit mapCleared(mapIndex);
        if (phase == Phase::Won) emit won();
    }
}

bool Game::nextMap() {
    if (phase != Phase::MapCleared || isLastMap()) return false;
    loadMap(mapIndex + 1);
    emit mapStarted(mapIndex);
    return true;
}

void Game::reset() {
    lives = data::START_LIVES;
    loadMap(0);
    emit mapStarted(mapIndex);
    // Ids keep counting so stale ids held by the UI never match new objects.
}
