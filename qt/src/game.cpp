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
    case EffectKind::Heal: return 0.6;
    case EffectKind::Hop: return 0.4;
    case EffectKind::Split: return 0.5;
    case EffectKind::Jackpot: return 1.3;
    case EffectKind::Goo: return 0.5;
    case EffectKind::BossDust: return 1.1;
    case EffectKind::FlyerFall: return 0.5;
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
    totalWaves = int(waves().size());
    time = 0;
    enemies.clear();
    pendingBugs_.clear();
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

    // Sky lanes for flyers (tile centers -> px), with segment lengths for walking by distance.
    skyLanes.clear();
    laneRoutes_.clear();
    nextLane_ = 0;
    spits.clear();
    for (const auto &lane : map().skyLanes) {
        if (lane.size() < 2) continue;
        std::vector<QPointF> pts;
        Route r;
        for (const auto &q : lane) pts.emplace_back(q[0] * TILE + TILE / 2, q[1] * TILE + TILE / 2);
        for (size_t i = 0; i + 1 < pts.size(); i++) {
            double L = std::hypot(pts[i + 1].x() - pts[i].x(), pts[i + 1].y() - pts[i].y());
            r.segStart.push_back(r.length);
            r.segLen.push_back(L);
            r.length += L;
        }
        skyLanes.push_back(pts);
        laneRoutes_.push_back(r);
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
    const data::Wave &w = waves()[wave];
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
    // Flyers take turns across the map's sky lanes; with no lanes they walk the path.
    if (def.flying && !skyLanes.empty()) e.lane = nextLane_++ % int(skyLanes.size());
    placeBug(e);
    if (def.boss) {
        // Boss entrance: a dust burst at the nest and a low rumble.
        addEffect(EffectKind::BossDust, std::max(e.x, TILE * 0.5), e.y, 46);
        emit bugSound(QStringLiteral("boss_rumble"));
    }
    e.hp = e.maxHp = def.hp * hpMult * map().hpMult;
    e.dist = 0;
    e.slowUntil = 0;
    e.slowFactor = 0;
    e.hitTime = -1;
    // Stagger ability timers by id so a crowd of crickets doesn't hop in lockstep.
    double every = def.hopEvery > 0 ? def.hopEvery : def.healEvery > 0 ? def.healEvery : def.spitEvery;
    if (every > 0) e.abilityTimer = every * (0.5 + 0.5 * ((e.id * 37) % 100) / 100.0);
    enemies.push_back(e);
}

// Bugs born mid-update (caterpillar grubs) wait here so nobody's loop over
// `enemies` is invalidated; they join at a safe point in update().
void Game::flushPendingBugs() {
    for (const auto &b : pendingBugs_) {
        const data::EnemyDef &def = data::ENEMIES[b.type];
        Enemy e{};
        e.id = nextId_++;
        e.type = b.type;
        e.dist = std::clamp(b.dist, 0.0, pathLength_ - 1);
        pointAt(e.dist, e.x, e.y, e.angle);
        e.hp = e.maxHp = b.maxHp;
        e.hitTime = -1;
        if (def.hopEvery > 0) e.abilityTimer = def.hopEvery;
        enemies.push_back(e);
    }
    pendingBugs_.clear();
}

// Position from dist along the bug's sky lane, or the ground path.
void Game::placeBug(Enemy &e) {
    if (e.lane < 0 || e.lane >= int(skyLanes.size())) { pointAt(e.dist, e.x, e.y, e.angle); return; }
    const auto &pts = skyLanes[e.lane];
    const Route &r = laneRoutes_[e.lane];
    double d = std::max(0.0, e.dist);
    size_t i = 0;
    while (i < r.segLen.size() - 1 && d > r.segStart[i] + r.segLen[i]) i++;
    double L = r.segLen[i] > 0 ? r.segLen[i] : 1;
    double t = std::clamp((d - r.segStart[i]) / L, 0.0, 1.0);
    e.x = pts[i].x() + (pts[i + 1].x() - pts[i].x()) * t;
    e.y = pts[i].y() + (pts[i + 1].y() - pts[i].y()) * t;
    e.angle = std::atan2(pts[i + 1].y() - pts[i].y(), pts[i + 1].x() - pts[i].x());
}

double Game::routeLength(const Enemy &e) const {
    return e.lane >= 0 && e.lane < int(laneRoutes_.size()) ? laneRoutes_[e.lane].length : pathLength_;
}

// Scorpion goo: a lobbed glob; on landing the plant is stunned (can't act).
void Game::updateSpits(double dt) {
    constexpr double FLIGHT = 0.55;   // seconds in the air
    for (auto &s : spits) {
        s.progress = std::min(1.0, s.progress + dt / FLIGHT);
        s.x = s.fromX + (s.toX - s.fromX) * s.progress;
        s.y = s.fromY + (s.toY - s.fromY) * s.progress;
        if (s.progress < 1) continue;
        if (Tower *t = getTower(s.towerId)) {   // the plant may have been sold meanwhile
            t->stunnedUntil = std::max(t->stunnedUntil, time + s.stunTime);
            addEffect(EffectKind::Goo, t->x, t->y, 26);
            emit bugSound(QStringLiteral("bug_split"));
        }
    }
    removeIf(spits, [](const Spit &s) { return s.progress >= 1; });
}

// Cricket hops and mosquito heal pulses.
void Game::updateBugAbilities(double dt) {
    for (auto &e : enemies) {
        if (!e.alive) continue;
        const data::EnemyDef &def = data::ENEMIES[e.type];
        if (def.spitEvery > 0) {
            // Scorpion: lob goo at the nearest plant in range. A plant can't be re-gooed until
            // 2 s after its last stun ends (or while goo is already flying at it), so no stun-lock.
            e.abilityTimer -= dt;
            if (e.abilityTimer > 0) continue;
            double bestD = std::pow(def.spitRange * TILE, 2);
            const Tower *best = nullptr;
            for (const auto &t : towers) {
                if (time < t.stunnedUntil + 2.0) continue;
                if (std::any_of(spits.begin(), spits.end(), [&](const Spit &s) { return s.towerId == t.id; })) continue;
                double dx = t.x - e.x, dy = t.y - e.y, d = dx * dx + dy * dy;
                if (d <= bestD) { bestD = d; best = &t; }
            }
            if (!best) { e.abilityTimer = 0; continue; }   // nothing in range: check again next frame (no wind-up shown)
            e.abilityTimer = def.spitEvery;
            spits.push_back({nextId_++, best->id, e.x, e.y, best->x, best->y, e.x, e.y, 0, def.stunTime});
            emit bugSound(QStringLiteral("bug_spit"));
            continue;
        }
        if (def.hopEvery <= 0 && def.healEvery <= 0) continue;
        e.abilityTimer -= dt;
        if (e.abilityTimer > 0) continue;

        if (def.hopEvery > 0) {
            e.abilityTimer += def.hopEvery;
            addEffect(EffectKind::Hop, e.x, e.y);   // dust puff where it took off
            e.dist += def.hopDist * TILE;
            e.hopStart = time;
            emit bugSound(QStringLiteral("bug_hop"));
        } else {
            e.abilityTimer += def.healEvery;
            const double r2 = std::pow(def.healRadius * TILE, 2);
            bool healedAny = false;
            for (auto &o : enemies) {
                if (!o.alive || o.hp >= o.maxHp) continue;
                double dx = o.x - e.x, dy = o.y - e.y;
                if (dx * dx + dy * dy > r2) continue;
                o.hp = std::min(o.maxHp, o.hp + o.maxHp * def.healPct);
                o.healTime = time;
                healedAny = true;
            }
            if (healedAny) {
                addEffect(EffectKind::Heal, e.x, e.y, def.healRadius * TILE);
                emit bugSound(QStringLiteral("bug_heal"));
            }
        }
    }
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

        if (e.dist >= routeLength(e)) {
            e.alive = false;
            if (phase == Phase::Lost) continue;
            lives -= def.lives;
            if (def.lives > 0) addEffect(EffectKind::Leak, leakX_, leakY_);   // golden bugs just slip away
            emit leaked(def.lives);
            if (lives <= 0) {
                lives = 0;
                phase = Phase::Lost;
                emit lost();
            }
            continue;
        }
        placeBug(e);
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
    if (def.flying)   // air kill: the sprite spirals down to its shadow (radius = drop height)
        addEffect(EffectKind::FlyerFall, e.x, e.y - visualY(e), visualY(e), def.icon);
    else
        addEffect(EffectKind::Kill, e.x, e.y);
    addEffect(EffectKind::Coin, e.x, e.y, 0, QStringLiteral("+%1").arg(def.reward));
    emit killed(def.reward);

    if (def.golden) {
        addEffect(EffectKind::Jackpot, e.x, e.y, 0, QStringLiteral("JACKPOT!"));
        emit bugSound(QStringLiteral("jackpot"));
    }
    if (def.splitInto >= 0 && def.splitCount > 0) {
        // Babies inherit the parent's wave/map toughness, fanned out along the path.
        const auto child = data::EnemyType(def.splitInto);
        const double mult = e.maxHp / def.hp;
        for (int i = 0; i < def.splitCount; i++)
            pendingBugs_.push_back({child, data::ENEMIES[child].hp * mult,
                                    e.dist + (i - (def.splitCount - 1) / 2.0) * 14});
        addEffect(EffectKind::Split, e.x, e.y, 22);
        emit bugSound(QStringLiteral("bug_split"));
    }
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
        if (t.stunned(time)) continue;   // gooed by a scorpion
        const data::TowerDef &def = data::TOWERS[t.type];
        if (def.projectile == data::Projectile::None || def.projectile == data::Projectile::Aura)
            continue;   // sunflowers and clovers don't attack
        const data::TowerLevel &lv = def.levels[t.level];
        t.cooldown = std::max(0.0, t.cooldown - dt * rateBoost(t));

        const Enemy *target = pickTarget(t, lv.range * TILE);
        if (!target) {
            // An ambushing plant keeps watching its fire point.
            if (t.hasFirePoint) t.angle = std::atan2(t.fireY - t.y, t.fireX - t.x);
            continue;
        }
        t.angle = std::atan2(target->y - t.y, target->x - t.x);
        if (t.cooldown > 0) continue;
        if (def.projectile == data::Projectile::Flame) fireFlame(t);
        else if (def.projectile == data::Projectile::Thorn) fireThorn(t);
        else if (def.projectile == data::Projectile::Kernel) fireKernels(t);
        else fire(t, *target);
    }
}

// Placeholder until flyers land (claude2-63 replaces this): every plant can hit every bug.
bool Game::canHit(const Tower &t, const Enemy &e) const {
    if (!data::ENEMIES[e.type].flying) return true;
    const auto k = data::TOWERS[t.type].projectile;
    return k != data::Projectile::Mortar && k != data::Projectile::Flame;   // ground attacks
}

// Can this plant shoot at this bug right now? In range, hittable (claude2-63's
// canHit: ground attacks can't reach flyers), and inside its fire point zone if set.
bool Game::eligible(const Tower &t, const Enemy &e, double rangePx) const {
    if (!e.alive || !canHit(t, e)) return false;
    double dx = e.x - t.x, dy = e.y - t.y;
    if (dx * dx + dy * dy > rangePx * rangePx) return false;
    if (t.hasFirePoint) {
        double fx = e.x - t.fireX, fy = e.y - t.fireY;
        if (fx * fx + fy * fy > FIRE_POINT_RADIUS * FIRE_POINT_RADIUS) return false;
    }
    return true;
}

// Default: "first" (the bug furthest along). With a fire point: the bug closest to it.
const Enemy *Game::pickTarget(const Tower &t, double rangePx) const {
    const Enemy *best = nullptr;
    double bestScore = 0;
    for (const auto &e : enemies) {
        if (!eligible(t, e, rangePx)) continue;
        double score = t.hasFirePoint ? -std::hypot(e.x - t.fireX, e.y - t.fireY) : e.dist;
        if (!best || score > bestScore) { best = &e; bestScore = score; }
    }
    return best;
}

bool Game::setFirePoint(int towerId, double x, double y) {
    Tower *t = getTower(towerId);
    if (!t || isOver()) return false;
    const double range = data::TOWERS[t->type].levels[t->level].range * TILE;
    if (std::hypot(x - t->x, y - t->y) > range) return false;   // must be within reach
    t->hasFirePoint = true;
    t->fireX = x;
    t->fireY = y;
    t->angle = std::atan2(y - t->y, x - t->x);
    return true;
}

// Ambush bonus: a plant committed to a fire point hits harder there.
int Game::damageOf(const Tower &t) const {
    const int base = data::TOWERS[t.type].levels[t.level].damage;
    return t.hasFirePoint ? int(std::lround(base * AMBUSH_BONUS)) : base;
}

void Game::clearFirePoint(int towerId) {
    if (Tower *t = getTower(towerId)) t->hasFirePoint = false;
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
        if (std::abs(off) <= data::FLAME_HALF_ANGLE && canHit(t, e)) damageEnemy(e, damageOf(t));
    }
    emit shot(t.type);
}

// Clover: the strongest clover aura covering this plant speeds up its reload.
double Game::rateBoost(const Tower &t) const {
    double best = 0;
    for (const auto &c : towers) {
        const data::TowerDef &cd = data::TOWERS[c.type];
        if (cd.projectile != data::Projectile::Aura || c.id == t.id || c.stunned(time)) continue;   // gooed clover: no aura
        const data::TowerLevel &cl = cd.levels[c.level];
        double dx = c.x - t.x, dy = c.y - t.y;
        if (dx * dx + dy * dy <= std::pow(cl.range * TILE, 2)) best = std::max(best, cl.boost);
    }
    return 1 + best;
}

// Thorn Rose: a straight thorn flies out to full range along the aim,
// hurting each bug it passes until its pierce count runs out.
void Game::fireThorn(Tower &t) {
    const data::TowerDef &def = data::TOWERS[t.type];
    const data::TowerLevel &lv = def.levels[t.level];
    t.shotTime = time;
    t.cooldown = 1 / lv.rate;
    Shot p{};
    p.id = nextId_++;
    p.kind = def.projectile;
    p.x = p.fromX = t.x;
    p.y = p.fromY = t.y;
    p.startDist = lv.range * TILE + TILE * 0.5;
    p.toX = t.x + std::cos(t.angle) * p.startDist;
    p.toY = t.y + std::sin(t.angle) * p.startDist;
    p.angle = t.angle;
    p.targetId = -1;
    p.damage = damageOf(t);
    p.speed = def.projectileSpeed * TILE;
    p.pierce = lv.pierce;
    shots.push_back(p);
    emit shot(t.type);
}

bool Game::stepThorn(Shot &p, double dt) {
    double travelled = std::hypot(p.x - p.fromX, p.y - p.fromY);
    double step = std::min(p.speed * dt, p.startDist - travelled);
    double ca = std::cos(p.angle), sa = std::sin(p.angle);
    p.x += ca * step;
    p.y += sa * step;
    p.progress = std::clamp((travelled + step) / p.startDist, 0.0, 1.0);
    constexpr double HIT_R = 16;
    for (auto &e : enemies) {
        if (p.pierce <= 0) break;
        if (!e.alive) continue;
        if (std::find(p.hitIds.begin(), p.hitIds.end(), e.id) != p.hitIds.end()) continue;
        // Distance from the bug to the segment swept this step.
        double ex = e.x - (p.x - ca * step), ey = e.y - (p.y - sa * step);
        double along = std::clamp(ex * ca + ey * sa, 0.0, step);
        double px = ex - ca * along, py = ey - sa * along;
        if (px * px + py * py > HIT_R * HIT_R) continue;
        p.hitIds.push_back(e.id);
        p.pierce--;
        damageEnemy(e, p.damage);
    }
    return p.pierce <= 0 || p.progress >= 1;
}

// Corn Popper: one kernel at each of the `shots` leading bugs in range.
void Game::fireKernels(Tower &t) {
    const data::TowerLevel &lv = data::TOWERS[t.type].levels[t.level];
    std::vector<const Enemy *> inRange;
    for (const auto &e : enemies)
        if (eligible(t, e, lv.range * TILE)) inRange.push_back(&e);
    std::sort(inRange.begin(), inRange.end(),
              [](const Enemy *a, const Enemy *b) { return a->dist > b->dist; });
    int n = std::min<int>(std::max(1, lv.shots), int(inRange.size()));
    for (int i = 0; i < n; i++) fire(t, *inRange[i]);   // fire() also sets cooldown and emits
}

int Game::harvest() {
    int total = 0;
    for (const auto &t : towers) {
        int income = data::TOWERS[t.type].levels[t.level].income;
        if (income <= 0 || t.stunned(time)) continue;   // a gooed sunflower skips this harvest
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
    p.damage = damageOf(t);
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
        bool finished = p.kind == data::Projectile::Mortar  ? stepMortar(p, dt)
                        : p.kind == data::Projectile::Thorn ? stepThorn(p, dt)
                                                            : stepHoming(p, dt);
        if (finished) done.push_back(p.id);
    }
    if (!done.empty())
        removeIf(shots, [&](const Shot &p) {
            return std::find(done.begin(), done.end(), p.id) != done.end();
        });
}

bool Game::stepHoming(Shot &p, double dt) {
    Enemy *target = findEnemy(p.targetId);
    if (target) { p.toX = target->x; p.toY = target->y - visualY(*target); }   // hit the sprite, not the shadow
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
        if (dx * dx + dy * dy <= r2 && !data::ENEMIES[e.type].flying) damageEnemy(e, p.damage);   // splash stays on the ground
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

    updateBugAbilities(dt);
    updateEnemies(dt);
    if (phase == Phase::Lost) { updateEffects(dt); return; }

    updatePoison(dt);
    updateTowers(dt);
    updateShots(dt);
    updateSpits(dt);
    removeIf(enemies, [](const Enemy &e) { return !e.alive; });
    flushPendingBugs();   // grubs hatched this frame (before the wave-over check)
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

void Game::reset(int startMap) {
    lives = data::START_LIVES;
    loadMap(std::clamp(startMap, 0, totalMaps - 1));
    emit mapStarted(mapIndex);
    // Ids keep counting so stale ids held by the UI never match new objects.
}
