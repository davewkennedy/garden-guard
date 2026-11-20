// Garden Guard++ web: simulation (port of game.h / game.cpp, owner: Bill).
// No DOM: runs in the browser and in Node. Reads the global DATA (js/data.js).
// Names match the C++ (trailing `_` dropped on privates). Signals: game.on(name, fn).
/* global DATA */
(function () {
'use strict';
const D = () => (typeof DATA !== 'undefined' ? DATA : globalThis.DATA);

const Phase = Object.freeze({ Build: 'Build', Wave: 'Wave', MapCleared: 'MapCleared', Won: 'Won', Lost: 'Lost' });
const Difficulty = Object.freeze({ Easy: 0, Normal: 1, Hard: 2 });   // integers (PLAN enum rules)
const EffectKind = Object.freeze({
    Hit: 'Hit', Splash: 'Splash', Kill: 'Kill', Coin: 'Coin', Leak: 'Leak', Place: 'Place', Slow: 'Slow',
    Poison: 'Poison', Flame: 'Flame', Heal: 'Heal', Hop: 'Hop', Split: 'Split', Jackpot: 'Jackpot', Goo: 'Goo',
    BossDust: 'BossDust', FlyerFall: 'FlyerFall', Lasso: 'Lasso',
});

const MAX_DT = 0.05;
const MAX_EFFECTS = 400;
const GUST_LEN = 2.5, GUST_WARN = 1.0;
const HARD_RAMP = true;
const EFFECT_TTL = { Hit: 0.25, Splash: 0.4, Kill: 0.45, Coin: 0.9, Leak: 0.8, Place: 0.5, Slow: 0.5, Poison: 0.6,
    Flame: 0.3, Heal: 0.6, Hop: 0.4, Split: 0.5, Jackpot: 1.3, Goo: 0.5, BossDust: 1.1, FlyerFall: 0.5, Lasso: 0.45 };
const EFFECT_RADIUS = { Hit: 10, Kill: 18, Leak: 28, Place: 32, Slow: 14, Poison: 14 };

const clamp = (v, lo, hi) => Math.min(Math.max(v, lo), hi);
const sign = v => (v > 0) - (v < 0);
// std::remainder: x - n*y with n = x/y rounded to nearest, ties to even.
function remainder(x, y) {
    const q = x / y;
    let n = Math.round(q);
    if (Math.abs(q - Math.trunc(q)) === 0.5) n = 2 * Math.round(q / 2);
    return x - n * y;
}
const lround = v => (v < 0 ? -Math.round(-v) : Math.round(v));   // half away from zero
const fmod = (a, b) => a % b;
const num = v => v || 0;   // data.js may omit zero fields (C++ designated-init defaults)

// Enum helpers: data.js may use numbers or strings for Projectile; compare via DATA.Projectile.
function P(name) { const p = D().Projectile; return p ? p[name] : name; }
function enemyDef(type) { return D().ENEMIES[type]; }
function enemyIndex(t) {   // WaveGroup.enemy: number, or a name like 'Ant'
    if (typeof t === 'number') return t;
    const ET = D().EnemyType;
    if (ET && ET[t] !== undefined) return ET[t];
    return D().ENEMIES.findIndex(e => e.name === t);
}

class Game {
    constructor() {
        this.handlers_ = {};
        this.quiet = false;
        const d = D();
        this.TILE = d.TILE;
        this.phase = Phase.Build;
        this.mapIndex = 0;
        this.totalMaps = d.MAPS.length;
        this.cols = d.DEFAULT_COLS; this.rows = d.DEFAULT_ROWS;
        this.W = this.cols * d.TILE; this.H = this.rows * d.TILE;
        this.coins = 0;
        this.lives = d.START_LIVES;
        this.wave = 0;
        this.totalWaves = d.WAVES.length;
        this.time = 0;
        this.enemies = []; this.towers = []; this.shots = []; this.effects = []; this.spits = [];
        this.skyLanes = []; this.path = []; this.path2 = [];
        this.difficulty = Difficulty.Normal;
        this.unlockAll = false;
        // private
        this.laneRoutes = []; this.nextLane = 0;
        this.route2 = { segLen: [], segStart: [], length: 0 }; this.nextRoute = 0;
        this.pendingBugs = [];
        this.segLen = []; this.segStart = []; this.pathLength = 0;
        this.prevWindOffset = 0;
        this.terrain = []; this.pathTiles = [];
        this.leakX = 0; this.leakY = 0;
        this.spawners = []; this.waveTime = 0;
        this.nextId = 1;
        this.loadMap(0);
    }

    // ---------- Signals ----------
    on(name, fn) { (this.handlers_[name] = this.handlers_[name] || []).push(fn); return fn; }
    off(name, fn) { const h = this.handlers_[name]; if (h) { const i = h.indexOf(fn); if (i >= 0) h.splice(i, 1); } }
    emit(name, ...args) {   // game.quiet = true mutes every signal (stands in for QSignalBlocker)
        if (this.quiet) return;
        const h = this.handlers_[name];
        if (h) for (const fn of h.slice()) fn(...args);
    }

    // ---------- Accessors ----------
    map() { return D().MAPS[this.mapIndex]; }
    waves() { const w = this.map().waves; return w && w.length ? w : D().WAVES; }
    isLastMap() { return this.mapIndex + 1 >= this.totalMaps; }
    isOver() { return this.phase === Phase.MapCleared || this.phase === Phase.Won || this.phase === Phase.Lost; }
    isUnlocked(type) {
        const T = D().TOWERS;
        return type >= 0 && type < T.length && (this.unlockAll || num(T[type].unlockMap) <= this.mapIndex);
    }
    // Enemy/Tower helpers (C++ member functions on the structs).
    grounded(e, now = this.time) { return now < e.groundedUntil; }
    slowed(e, now = this.time) { return now < e.slowUntil && e.slowFactor > 0; }
    poisoned(e, now = this.time) { return now < e.poisonUntil && e.poisonDps > 0; }
    stunned(t, now = this.time) { return now < t.stunnedUntil; }
    visualY(e) { return enemyDef(e.type).flying && !this.grounded(e) ? 16 : 0; }

    // ---------- Map ----------
    loadMap(index) {
        const TILE = D().TILE;
        this.mapIndex = index;
        const m = this.map();
        const tiles = m.path;
        this.cols = m.cols || D().DEFAULT_COLS;
        this.rows = m.rows || D().DEFAULT_ROWS;
        this.W = this.cols * TILE;
        this.H = this.rows * TILE;

        this.phase = Phase.Build;
        this.coins = this.startCoins();
        this.wave = 0;
        this.totalWaves = this.waves().length;
        this.time = 0;
        this.enemies = []; this.pendingBugs = []; this.towers = []; this.shots = []; this.effects = [];
        this.spawners = []; this.waveTime = 0;
        this.prevWindOffset = 0;

        const toPx = p => ({ x: p[0] * TILE + TILE / 2, y: p[1] * TILE + TILE / 2 });
        const build = pts => {
            const r = { segLen: [], segStart: [], length: 0 };
            for (let i = 0; i + 1 < pts.length; i++) {
                const L = Math.hypot(pts[i + 1].x - pts[i].x, pts[i + 1].y - pts[i].y);
                r.segStart.push(r.length); r.segLen.push(L); r.length += L;
            }
            return r;
        };
        this.path = tiles.map(toPx);
        const r1 = build(this.path);
        this.segLen = r1.segLen; this.segStart = r1.segStart; this.pathLength = r1.length;

        const p2 = m.path2 || [];
        this.path2 = p2.map(toPx);
        if (this.path2.length < 2) this.path2 = [];
        this.route2 = build(this.path2);
        this.nextRoute = 0;

        this.skyLanes = []; this.laneRoutes = []; this.nextLane = 0; this.spits = [];
        for (const lane of m.skyLanes || []) {
            if (lane.length < 2) continue;
            const pts = lane.map(toPx);
            this.skyLanes.push(pts);
            this.laneRoutes.push(build(pts));
        }

        const cols = this.cols, rows = this.rows;
        this.pathTiles = new Array(cols * rows).fill(false);
        for (const route of [tiles, p2])
            for (let i = 0; i + 1 < route.length; i++) {
                const c0 = route[i][0], r0 = route[i][1], c1 = route[i + 1][0], r1b = route[i + 1][1];
                const steps = Math.max(Math.abs(c1 - c0), Math.abs(r1b - r0));
                for (let s = 0; s <= steps; s++) {
                    const c = c0 + sign(c1 - c0) * s, r = r0 + sign(r1b - r0) * s;
                    if (c >= 0 && c < cols && r >= 0 && r < rows) this.pathTiles[r * cols + c] = true;
                }
            }

        this.terrain = new Array(cols * rows).fill(-1);
        for (const t of m.terrain || []) {
            const c = t[0], r = t[1];
            if (c >= 0 && c < cols && r >= 0 && r < rows && !this.pathTiles[r * cols + c]) this.terrain[r * cols + c] = t[2];
        }

        const last = this.path[this.path.length - 1];
        this.leakX = clamp(last.x, TILE / 2, this.W - TILE / 2);
        this.leakY = clamp(last.y, TILE / 2, this.H - TILE / 2);
    }

    pointAt(d) {   // C++ out-params -> returns {x, y, angle}
        if (d < 0) d = 0;
        let i = 0;
        while (i < this.segLen.length - 1 && d > this.segStart[i] + this.segLen[i]) i++;
        const a = this.path[i], b = this.path[i + 1];
        const L = this.segLen[i] > 0 ? this.segLen[i] : 1;
        const t = clamp((d - this.segStart[i]) / L, 0, 1);
        return { x: a.x + (b.x - a.x) * t, y: a.y + (b.y - a.y) * t, angle: Math.atan2(b.y - a.y, b.x - a.x) };
    }

    addEffect(kind, x, y, radius = -1, text = '', angle = 0) {
        if (this.effects.length >= MAX_EFFECTS) this.effects.shift();
        this.effects.push({ kind, x, y, t: 0, ttl: EFFECT_TTL[kind] ?? 0.5,
            radius: radius >= 0 ? radius : (EFFECT_RADIUS[kind] || 0), text, angle });
    }

    // ---------- Grid & towers ----------
    isPathTile(col, row) {
        if (col < 0 || col >= this.cols || row < 0 || row >= this.rows) return false;
        return this.pathTiles[row * this.cols + col];
    }
    canPlace(col, row) {
        if (col < 0 || col >= this.cols || row < 0 || row >= this.rows) return false;
        if (this.isPathTile(col, row)) return false;
        const k = this.terrainAt(col, row);
        if (k === D().TERRAIN_ROCK || k === D().TERRAIN_WATER) return false;
        return !this.towers.some(t => t.col === col && t.row === row);
    }
    terrainAt(col, row) {
        if (col < 0 || col >= this.cols || row < 0 || row >= this.rows || !this.terrain.length) return -1;
        return this.terrain[row * this.cols + col];
    }
    towerAt(col, row) { return this.towers.find(t => t.col === col && t.row === row) || null; }
    getTower(id) { return this.towers.find(t => t.id === id) || null; }

    placeTower(type, col, row) {
        if (!this.isUnlocked(type) || this.isOver() || !this.canPlace(col, row)) return null;
        const TILE = D().TILE;
        const cost = D().TOWERS[type].levels[0].cost;
        if (this.coins < cost) return null;
        this.coins -= cost;
        const t = { id: this.nextId++, type, level: 0, col, row, x: col * TILE + TILE / 2, y: row * TILE + TILE / 2,
            angle: -Math.PI / 2, cooldown: 0, shotTime: -1, spent: cost,
            hasFirePoint: false, fireX: 0, fireY: 0, focusId: -1, stunnedUntil: 0 };
        this.towers.push(t);
        this.addEffect(EffectKind.Place, t.x, t.y, TILE / 2);
        this.emit('placed', t.id);
        return t;
    }
    upgradeCost(t) {
        const levels = D().TOWERS[t.type].levels;
        return t.level + 1 < levels.length ? levels[t.level + 1].cost : -1;
    }
    upgradeTower(id) {
        const t = this.getTower(id);
        if (!t || this.isOver()) return false;
        const cost = this.upgradeCost(t);
        if (cost < 0 || this.coins < cost) return false;
        this.coins -= cost; t.level++; t.spent += cost;
        this.emit('upgraded', t.id);
        return true;
    }
    sellValue(t) { return Math.floor(t.spent * D().SELL_RATE); }
    sellTower(id) {
        const t = this.getTower(id);
        if (!t || this.isOver()) return 0;
        const refund = this.sellValue(t);
        this.coins += refund;
        this.towers = this.towers.filter(x => x.id !== id);
        this.emit('sold', refund);
        return refund;
    }

    // ---------- Waves ----------
    startWave() {
        if (this.phase !== Phase.Build || this.wave >= this.totalWaves) return false;
        const w = this.waves()[this.wave];
        this.wave++;
        this.phase = Phase.Wave;
        this.waveTime = 0;
        this.spawners = w.groups.map(g => ({ type: enemyIndex(g.enemy), remaining: g.count, next: num(g.delay),
            gap: g.gap, hpMult: w.hpMult }));
        this.emit('waveStarted', this.wave);
        return true;
    }

    newEnemy(id, type) {
        return { id, type, x: 0, y: 0, angle: 0, hp: 0, maxHp: 0, dist: 0, slowUntil: 0, slowFactor: 0, hitTime: -1,
            poisonDps: 0, poisonUntil: 0, alive: true, abilityTimer: 0, hopStart: -1, healTime: -1, lane: -1, route: 0,
            heading: 0, spawnTime: 0, startDist: 0, trailTimer: 0, trail: [], groundedUntil: 0, vineBy: -1 };
    }

    spawnEnemy(type, hpMult) {
        const TILE = D().TILE;
        const def = enemyDef(type);
        const e = this.newEnemy(this.nextId++, type);
        if (def.flying && !def.wild && this.skyLanes.length) e.lane = this.nextLane++ % this.skyLanes.length;
        else if (!def.flying && this.path2.length) e.route = this.nextRoute++ % 2;
        this.placeBug(e);
        if (def.wild) {
            e.spawnTime = this.time;
            e.heading = Math.atan2(this.leakY - e.y, this.leakX - e.x);
            e.startDist = Math.hypot(this.leakX - e.x, this.leakY - e.y);
            this.emit('bugSound', 'bug_screech');
            this.emit('wildcard', e.id);
        }
        if (def.boss) {
            this.addEffect(EffectKind.BossDust, Math.max(e.x, TILE * 0.5), e.y, 46);
            this.emit('bugSound', 'boss_rumble');
        }
        e.hp = e.maxHp = def.hp * hpMult * this.map().hpMult * this.difficultyHpMult();
        e.dist = 0; e.slowUntil = 0; e.slowFactor = 0; e.hitTime = -1;
        const every = num(def.hopEvery) > 0 ? def.hopEvery : num(def.healEvery) > 0 ? def.healEvery : num(def.spitEvery);
        if (every > 0) e.abilityTimer = every * (0.5 + 0.5 * ((e.id * 37) % 100) / 100.0);
        this.enemies.push(e);
    }

    flushPendingBugs() {
        for (const b of this.pendingBugs) {
            const def = enemyDef(b.type);
            const e = this.newEnemy(this.nextId++, b.type);
            e.route = b.route;
            e.dist = clamp(b.dist, 0, this.routeLength(e) - 1);
            this.placeBug(e);
            e.hp = e.maxHp = b.maxHp;
            e.hitTime = -1;
            if (num(def.hopEvery) > 0) e.abilityTimer = def.hopEvery;
            this.enemies.push(e);
        }
        this.pendingBugs = [];
    }

    steerWildBat(e, dt, mult) {
        const TILE = D().TILE, W = this.W, H = this.H;
        const def = enemyDef(e.type);
        const age = this.time - e.spawnTime;
        let hx = this.leakX - e.x, hy = this.leakY - e.y;
        if (Math.hypot(hx, hy) < 0.6 * TILE) return true;
        const home = Math.atan2(hy, hx);
        let w = clamp(age / 18.0, 0, 1);
        w *= w;
        if (this.grounded(e) || age > 18) w = 1;
        const h = Math.imul(e.id >>> 0, 2654435761) >>> 0;   // unsigned 32-bit, like C++
        const f1 = 0.25 + (h & 0xff) / 255.0 * 0.2, f2 = 0.7 + ((h >>> 8) & 0xff) / 255.0 * 0.4;
        const p1 = ((h >>> 16) & 0xff) / 255.0 * 6.2832, p2 = ((h >>> 24) & 0xff) / 255.0 * 6.2832;
        const wander = home + 1.7 * Math.sin(age * f1 * 6.2832 + p1) + 0.8 * Math.sin(age * f2 * 6.2832 + p2);
        let dx = (1 - w) * Math.cos(wander) + w * Math.cos(home);
        let dy = (1 - w) * Math.sin(wander) + w * Math.sin(home);
        const near = d => clamp(1 - d / TILE, 0, 1);
        const push = 1.6 * (1 - w);
        dx += push * (near(e.x) - near(W - e.x));
        dy += push * (near(e.y) - near(H - e.y));
        const diff = remainder(Math.atan2(dy, dx) - e.heading, 2 * Math.PI);
        const maxTurn = 2.6 * dt;
        e.heading += clamp(diff, -maxTurn, maxTurn);
        const speed = def.speed * TILE * mult * (age > 30 ? 1.6 : 1.0);
        e.x += Math.cos(e.heading) * speed * dt;
        e.y += Math.sin(e.heading) * speed * dt;
        e.x = Math.min(e.x, W - TILE * 0.3);
        e.y = clamp(e.y, TILE * 0.3, H - TILE * 0.3);
        e.angle = e.heading;
        hx = this.leakX - e.x; hy = this.leakY - e.y;
        const hd = Math.hypot(hx, hy);
        e.dist = Math.max(0, e.startDist - hd);
        e.trailTimer -= dt;
        if (e.trailTimer <= 0) {
            e.trailTimer = 0.08;
            e.trail.push({ x: e.x, y: e.y });
            if (e.trail.length > 10) e.trail.shift();
        }
        return hd < 0.6 * TILE;
    }

    fireVine(t, dt) {
        const TILE = D().TILE;
        const lv = D().TOWERS[t.type].levels[t.level];
        t.cooldown = Math.max(0, t.cooldown - dt * this.rateBoost(t));
        const r2 = Math.pow(lv.range * TILE, 2);
        let best = null;
        for (const e of this.enemies) {
            if (!e.alive || !enemyDef(e.type).flying || this.grounded(e)) continue;
            const dx = e.x - t.x, dy = e.y - t.y;
            if (dx * dx + dy * dy <= r2 && (!best || e.dist > best.dist)) best = e;
        }
        if (!best) return;
        t.angle = Math.atan2(best.y - t.y, best.x - t.x);
        if (t.cooldown > 0) return;
        t.shotTime = this.time;
        t.cooldown = 1 / lv.rate;
        best.groundedUntil = this.time + num(lv.groundTime);
        best.vineBy = t.id;
        this.addEffect(EffectKind.Lasso, best.x, best.y, 20);
        this.emit('shot', t.type);
        this.damageEnemy(best, this.damageOf(t));
    }

    placeBug(e) {
        const second = e.lane < 0 && e.route === 1 && this.path2.length > 0;
        if (!second && (e.lane < 0 || e.lane >= this.skyLanes.length)) {
            const p = this.pointAt(e.dist); e.x = p.x; e.y = p.y; e.angle = p.angle; return;
        }
        const pts = second ? this.path2 : this.skyLanes[e.lane];
        const r = second ? this.route2 : this.laneRoutes[e.lane];
        const d = Math.max(0, e.dist);
        let i = 0;
        while (i < r.segLen.length - 1 && d > r.segStart[i] + r.segLen[i]) i++;
        const L = r.segLen[i] > 0 ? r.segLen[i] : 1;
        const t = clamp((d - r.segStart[i]) / L, 0, 1);
        e.x = pts[i].x + (pts[i + 1].x - pts[i].x) * t;
        e.y = pts[i].y + (pts[i + 1].y - pts[i].y) * t;
        e.angle = Math.atan2(pts[i + 1].y - pts[i].y, pts[i + 1].x - pts[i].x);
    }

    routeLength(e) {
        if (e.lane < 0 && e.route === 1 && this.path2.length) return this.route2.length;
        return e.lane >= 0 && e.lane < this.laneRoutes.length ? this.laneRoutes[e.lane].length : this.pathLength;
    }

    updateSpits(dt) {
        const FLIGHT = 0.55;
        for (const s of this.spits) {
            s.progress = Math.min(1, s.progress + dt / FLIGHT);
            s.x = s.fromX + (s.toX - s.fromX) * s.progress;
            s.y = s.fromY + (s.toY - s.fromY) * s.progress;
            if (s.progress < 1) continue;
            const t = this.getTower(s.towerId);
            if (t) {
                t.stunnedUntil = Math.max(t.stunnedUntil, this.time + s.stunTime);
                this.addEffect(EffectKind.Goo, t.x, t.y, 26);
                this.emit('bugSound', 'bug_split');
            }
        }
        this.spits = this.spits.filter(s => s.progress < 1);
    }

    updateBugAbilities(dt) {
        const TILE = D().TILE;
        for (const e of this.enemies) {
            if (!e.alive) continue;
            const def = enemyDef(e.type);
            if (num(def.spitEvery) > 0) {
                e.abilityTimer -= dt;
                if (e.abilityTimer > 0) continue;
                let bestD = Math.pow(def.spitRange * TILE, 2);
                let best = null;
                for (const t of this.towers) {
                    if (this.time < t.stunnedUntil + 2.0) continue;
                    if (this.spits.some(s => s.towerId === t.id)) continue;
                    const dx = t.x - e.x, dy = t.y - e.y, d = dx * dx + dy * dy;
                    if (d <= bestD) { bestD = d; best = t; }
                }
                if (!best) { e.abilityTimer = 0; continue; }
                e.abilityTimer = def.spitEvery;
                this.spits.push({ id: this.nextId++, towerId: best.id, fromX: e.x, fromY: e.y, toX: best.x, toY: best.y,
                    x: e.x, y: e.y, progress: 0, stunTime: def.stunTime });
                this.emit('bugSound', 'bug_spit');
                continue;
            }
            if (num(def.hopEvery) <= 0 && num(def.healEvery) <= 0) continue;
            e.abilityTimer -= dt;
            if (e.abilityTimer > 0) continue;
            if (num(def.hopEvery) > 0) {
                e.abilityTimer += def.hopEvery;
                this.addEffect(EffectKind.Hop, e.x, e.y);
                e.dist += def.hopDist * TILE;
                e.hopStart = this.time;
                this.emit('bugSound', 'bug_hop');
            } else {
                e.abilityTimer += def.healEvery;
                const r2 = Math.pow(def.healRadius * TILE, 2);
                let healedAny = false;
                for (const o of this.enemies) {
                    if (!o.alive || o.hp >= o.maxHp) continue;
                    const dx = o.x - e.x, dy = o.y - e.y;
                    if (dx * dx + dy * dy > r2) continue;
                    o.hp = Math.min(o.maxHp, o.hp + o.maxHp * def.healPct);
                    o.healTime = this.time;
                    healedAny = true;
                }
                if (healedAny) {
                    this.addEffect(EffectKind.Heal, e.x, e.y, def.healRadius * TILE);
                    this.emit('bugSound', 'bug_heal');
                }
            }
        }
    }

    updateSpawns(dt) {
        this.waveTime += dt;
        for (const s of this.spawners) {
            while (s.remaining > 0 && this.waveTime >= s.next) {
                this.spawnEnemy(s.type, s.hpMult);
                s.remaining--;
                s.next += s.gap;
            }
        }
    }
    spawningDone() { return this.spawners.every(s => s.remaining <= 0); }

    // ---------- Enemies ----------
    findEnemy(id) { return this.enemies.find(e => e.id === id && e.alive) || null; }

    windStrength() {
        const every = num(this.map().windEvery);
        if (every <= 0 || this.time < every) return 0;
        const t = fmod(this.time, every);
        return t < GUST_LEN ? Math.pow(Math.sin(Math.PI * t / GUST_LEN), 2) : 0;
    }
    windWarning() {
        const every = num(this.map().windEvery);
        if (every <= 0) return 0;
        const t = fmod(this.time, every);
        return t >= every - GUST_WARN ? (t - (every - GUST_WARN)) / GUST_WARN : 0;
    }
    windDir() {
        const every = num(this.map().windEvery);
        if (every <= 0) return 1;
        return Math.floor((this.time + GUST_WARN) / every) % 2 ? 1 : -1;
    }
    windOffset() { return this.windDir() * 1.5 * D().TILE * this.windStrength(); }

    updateEnemies(dt) {
        const TILE = D().TILE;
        const windNow = this.windOffset();
        const windDelta = windNow - this.prevWindOffset;
        this.prevWindOffset = windNow;
        const every = num(this.map().windEvery);
        if (every > 0 && this.time - dt >= 0) {
            const tPrev = fmod(this.time - dt, every), tNow = fmod(this.time, every);
            if (tPrev < every - GUST_WARN && tNow >= every - GUST_WARN) {
                this.emit('gust');
                this.emit('bugSound', 'gust_whoosh');
            }
        }
        for (const e of this.enemies) {
            if (!e.alive) continue;
            const def = enemyDef(e.type);
            let mult = 1;
            if (this.time < e.slowUntil) mult = 1 - e.slowFactor;
            else e.slowFactor = 0;
            if (this.grounded(e)) mult *= 0.7;
            let arrived;
            if (def.wild) {
                arrived = this.steerWildBat(e, dt, mult);
                if (!this.grounded(e) && windDelta !== 0)
                    e.y = clamp(e.y + windDelta, TILE * 0.3, this.H - TILE * 0.3);
            } else {
                const snow = def.flying ? 1.0 : (this.map().bugSpeedMult ?? 1);
                e.dist += def.speed * TILE * mult * snow * dt;
                arrived = e.dist >= this.routeLength(e);
            }
            if (arrived) {
                e.alive = false;
                if (this.phase === Phase.Lost) continue;
                this.lives -= def.lives;
                if (def.lives > 0) this.addEffect(EffectKind.Leak, this.leakX, this.leakY);
                this.emit('leaked', def.lives);
                if (this.lives <= 0) {
                    this.lives = 0;
                    this.phase = Phase.Lost;
                    this.emit('lost');
                }
                continue;
            }
            if (!def.wild) {
                this.placeBug(e);
                if (def.flying && !this.grounded(e)) e.y += windNow;
            }
        }
        this.enemies = this.enemies.filter(e => e.alive);
    }

    damageEnemy(e, amount) {
        if (!e.alive) return false;
        const def = enemyDef(e.type);
        const dmg = Math.max(1.0, amount - num(def.armor));
        e.hp -= dmg;
        e.hitTime = this.time;
        this.addEffect(EffectKind.Hit, e.x, e.y);
        if (e.hp <= 0) { this.killEnemy(e); return false; }
        return true;
    }

    killEnemy(e) {
        const def = enemyDef(e.type);
        e.hp = 0;
        e.alive = false;
        this.coins += def.reward;
        if (def.flying) this.addEffect(EffectKind.FlyerFall, e.x, e.y - this.visualY(e), this.visualY(e), def.icon);
        else this.addEffect(EffectKind.Kill, e.x, e.y);
        this.addEffect(EffectKind.Coin, e.x, e.y, 0, '+' + def.reward);
        this.emit('killed', def.reward);
        if (def.golden) {
            this.addEffect(EffectKind.Jackpot, e.x, e.y, 0, 'JACKPOT!');
            this.emit('bugSound', 'jackpot');
        }
        const splitInto = def.splitInto ?? -1, splitCount = num(def.splitCount);
        if (splitInto >= 0 && splitCount > 0) {
            const child = enemyIndex(splitInto);
            const mult = e.maxHp / def.hp;
            for (let i = 0; i < splitCount; i++)
                this.pendingBugs.push({ type: child, maxHp: enemyDef(child).hp * mult,
                    dist: e.dist + (i - (splitCount - 1) / 2.0) * 14, route: e.route });
            this.addEffect(EffectKind.Split, e.x, e.y, 22);
            this.emit('bugSound', 'bug_split');
        }
    }

    updatePoison(dt) {
        for (const e of this.enemies) {
            if (!e.alive || !this.poisoned(e)) continue;
            e.hp -= e.poisonDps * dt;
            if (e.hp <= 0) this.killEnemy(e);
        }
    }
    applyPoison(e, dps, poisonTime) {
        if (!e.alive || dps <= 0) return;
        const active = this.poisoned(e) ? e.poisonDps : 0;
        e.poisonDps = Math.max(active, dps);
        e.poisonUntil = this.time + poisonTime;
        this.addEffect(EffectKind.Poison, e.x, e.y);
    }
    applySlow(e, slow, slowTime) {
        if (!e.alive || slow <= 0) return;
        const def = enemyDef(e.type);
        const active = this.time < e.slowUntil ? e.slowFactor : 0;
        e.slowFactor = Math.max(active, slow * (this.map().frostMult ?? 1) * (1 - num(def.slowResist)));
        e.slowUntil = this.time + slowTime;
        this.addEffect(EffectKind.Slow, e.x, e.y);
    }

    // ---------- Towers ----------
    updateTowers(dt) {
        const TILE = D().TILE;
        for (const t of this.towers) {
            if (t.focusId >= 0 && !this.findEnemy(t.focusId)) t.focusId = -1;
            if (this.stunned(t)) continue;
            const def = D().TOWERS[t.type];
            if (def.projectile === P('None') || def.projectile === P('Aura')) continue;
            if (def.projectile === P('Vine')) { this.fireVine(t, dt); continue; }
            const lv = def.levels[t.level];
            t.cooldown = Math.max(0, t.cooldown - dt * this.rateBoost(t));
            const target = this.pickTarget(t, lv.range * TILE);
            if (!target) {
                if (t.hasFirePoint) t.angle = Math.atan2(t.fireY - t.y, t.fireX - t.x);
                continue;
            }
            t.angle = Math.atan2(target.y - t.y, target.x - t.x);
            if (t.cooldown > 0) continue;
            if (def.projectile === P('Flame')) this.fireFlame(t);
            else if (def.projectile === P('Thorn')) this.fireThorn(t);
            else if (def.projectile === P('Kernel')) this.fireKernels(t);
            else this.fire(t, target);
        }
    }

    canHit(t, e) {
        if (!enemyDef(e.type).flying || this.grounded(e)) return true;
        const k = D().TOWERS[t.type].projectile;
        return k !== P('Mortar') && k !== P('Flame');
    }
    eligible(t, e, rangePx) {
        if (!e.alive || !this.canHit(t, e)) return false;
        const dx = e.x - t.x, dy = e.y - t.y;
        if (dx * dx + dy * dy > rangePx * rangePx) return false;
        if (t.hasFirePoint) {
            const R = Game.FIRE_POINT_RADIUS;
            const fx = e.x - t.fireX, fy = e.y - t.fireY;
            if (fx * fx + fy * fy > R * R) return false;
        }
        return true;
    }
    pickTarget(t, rangePx) {
        if (t.focusId >= 0) {
            for (const e of this.enemies) {
                if (e.id !== t.focusId || !e.alive || !this.canHit(t, e)) continue;
                if (Math.hypot(e.x - t.x, e.y - t.y) <= rangePx) return e;
            }
        }
        let best = null, bestScore = 0;
        for (const e of this.enemies) {
            if (!this.eligible(t, e, rangePx)) continue;
            const score = t.hasFirePoint ? -Math.hypot(e.x - t.fireX, e.y - t.fireY) : e.dist;
            if (!best || score > bestScore) { best = e; bestScore = score; }
        }
        return best;
    }

    setFirePoint(towerId, x, y) {
        const t = this.getTower(towerId);
        if (!t || this.isOver()) return false;
        const range = D().TOWERS[t.type].levels[t.level].range * D().TILE;
        if (Math.hypot(x - t.x, y - t.y) > range) return false;
        t.hasFirePoint = true; t.fireX = x; t.fireY = y;
        t.angle = Math.atan2(y - t.y, x - t.x);
        return true;
    }
    clearFirePoint(towerId) { const t = this.getTower(towerId); if (t) t.hasFirePoint = false; }
    damageOf(t) {
        const base = num(D().TOWERS[t.type].levels[t.level].damage);
        return t.hasFirePoint && t.focusId < 0 ? lround(base * Game.AMBUSH_BONUS) : base;
    }

    // ---------- Difficulty ----------
    difficultyHpMult() {
        if (this.difficulty === Difficulty.Easy) return 0.75;
        if (this.difficulty === Difficulty.Hard) return HARD_RAMP && this.mapIndex < 2 ? 1.0 : 1.15;
        return 1.0;
    }
    difficultyCoinMult() {
        if (this.difficulty === Difficulty.Easy) return 1.25;
        if (this.difficulty === Difficulty.Hard) return 0.9;
        return 1.0;
    }
    startCoins() {
        if (this.difficulty === Difficulty.Normal) return this.map().startCoins;
        return lround(this.map().startCoins * this.difficultyCoinMult());
    }
    setDifficulty(d) {
        this.difficulty = clamp(Math.trunc(Number(d)) || 0, 0, 2);
        if (this.phase === Phase.Build && this.wave === 0 && this.towers.length === 0) this.coins = this.startCoins();
    }

    // ---------- Focus fire ----------
    focusFire(towerIds, enemyId) {
        if (this.isOver() || !this.findEnemy(enemyId)) return false;
        let n = 0;
        for (const id of towerIds) {
            const t = this.getTower(id);
            if (!t) continue;
            const k = D().TOWERS[t.type].projectile;
            if (k === P('None') || k === P('Aura')) continue;
            t.focusId = enemyId;
            n++;
        }
        return n > 0;
    }
    clearFocus(towerId) { const t = this.getTower(towerId); if (t) t.focusId = -1; }
    enemyNear(x, y, radius) {
        let best = -1, bestD = radius;
        for (const e of this.enemies) {
            if (!e.alive) continue;
            const d = Math.hypot(e.x - x, e.y - this.visualY(e) - y);
            if (d <= bestD) { bestD = d; best = e.id; }
        }
        return best;
    }

    // ---------- Attacks ----------
    fireFlame(t) {
        const lv = D().TOWERS[t.type].levels[t.level];
        const range = lv.range * D().TILE;
        t.shotTime = this.time;
        t.cooldown = 1 / lv.rate;
        this.addEffect(EffectKind.Flame, t.x, t.y, range, '', t.angle);
        for (const e of this.enemies) {
            if (!e.alive) continue;
            const dx = e.x - t.x, dy = e.y - t.y;
            if (dx * dx + dy * dy > range * range) continue;
            const off = remainder(Math.atan2(dy, dx) - t.angle, 2 * Math.PI);
            if (Math.abs(off) <= D().FLAME_HALF_ANGLE && this.canHit(t, e)) this.damageEnemy(e, this.damageOf(t));
        }
        this.emit('shot', t.type);
    }
    rateBoost(t) {
        let best = 0;
        for (const c of this.towers) {
            const cd = D().TOWERS[c.type];
            if (cd.projectile !== P('Aura') || c.id === t.id || this.stunned(c)) continue;
            const cl = cd.levels[c.level];
            const dx = c.x - t.x, dy = c.y - t.y;
            if (dx * dx + dy * dy <= Math.pow(cl.range * D().TILE, 2)) best = Math.max(best, num(cl.boost));
        }
        return 1 + best;
    }
    newShot(t) {
        return { id: this.nextId++, kind: D().TOWERS[t.type].projectile, x: t.x, y: t.y, fromX: t.x, fromY: t.y,
            toX: 0, toY: 0, progress: 0, angle: t.angle, targetId: -1, damage: 0, speed: 0, slow: 0, slowTime: 0,
            poisonDps: 0, poisonTime: 0, splash: 0, startDist: 0, pierce: 0, hitIds: [] };
    }
    fireThorn(t) {
        const TILE = D().TILE;
        const def = D().TOWERS[t.type];
        const lv = def.levels[t.level];
        t.shotTime = this.time;
        t.cooldown = 1 / lv.rate;
        const p = this.newShot(t);
        p.startDist = lv.range * TILE + TILE * 0.5;
        p.toX = t.x + Math.cos(t.angle) * p.startDist;
        p.toY = t.y + Math.sin(t.angle) * p.startDist;
        p.damage = this.damageOf(t);
        p.speed = def.projectileSpeed * TILE;
        p.pierce = num(lv.pierce);
        this.shots.push(p);
        this.emit('shot', t.type);
    }
    stepThorn(p, dt) {
        const travelled = Math.hypot(p.x - p.fromX, p.y - p.fromY);
        const step = Math.min(p.speed * dt, p.startDist - travelled);
        const ca = Math.cos(p.angle), sa = Math.sin(p.angle);
        p.x += ca * step;
        p.y += sa * step;
        p.progress = clamp((travelled + step) / p.startDist, 0, 1);
        const HIT_R = 16;
        for (const e of this.enemies) {
            if (p.pierce <= 0) break;
            if (!e.alive) continue;
            if (p.hitIds.includes(e.id)) continue;
            const ex = e.x - (p.x - ca * step), ey = e.y - (p.y - sa * step);
            const along = clamp(ex * ca + ey * sa, 0, step);
            const px = ex - ca * along, py = ey - sa * along;
            if (px * px + py * py > HIT_R * HIT_R) continue;
            p.hitIds.push(e.id);
            p.pierce--;
            this.damageEnemy(e, p.damage);
        }
        return p.pierce <= 0 || p.progress >= 1;
    }
    fireKernels(t) {
        const lv = D().TOWERS[t.type].levels[t.level];
        const inRange = this.enemies.filter(e => this.eligible(t, e, lv.range * D().TILE));
        inRange.sort((a, b) => b.dist - a.dist);   // stable, like std::sort enough for parity
        const n = Math.min(Math.max(1, num(lv.shots)), inRange.length);
        for (let i = 0; i < n; i++) this.fire(t, inRange[i]);
    }
    harvest() {
        let total = 0;
        for (const t of this.towers) {
            const income = num(D().TOWERS[t.type].levels[t.level].income);
            if (income <= 0 || this.stunned(t)) continue;
            total += income;
            this.addEffect(EffectKind.Coin, t.x, t.y - 8, 0, '+' + income);
        }
        this.coins += total;
        return total;
    }
    fire(t, target) {
        const TILE = D().TILE;
        const def = D().TOWERS[t.type];
        const lv = def.levels[t.level];
        t.shotTime = this.time;
        t.cooldown = 1 / lv.rate;
        const p = this.newShot(t);
        p.toX = target.x; p.toY = target.y;
        p.targetId = target.id;
        p.damage = this.damageOf(t);
        p.speed = num(def.projectileSpeed) * TILE;
        p.slow = num(lv.slow); p.slowTime = num(lv.slowTime);
        p.poisonDps = num(lv.poisonDps); p.poisonTime = num(lv.poisonTime);
        p.splash = num(lv.splash) * TILE;
        p.startDist = Math.hypot(target.x - t.x, target.y - t.y);
        if (p.startDist <= 0) p.startDist = 1;
        this.shots.push(p);
        this.emit('shot', t.type);
    }

    // ---------- Projectiles ----------
    updateShots(dt) {
        const done = new Set();
        for (const p of this.shots) {
            const finished = p.kind === P('Mortar') ? this.stepMortar(p, dt)
                : p.kind === P('Thorn') ? this.stepThorn(p, dt) : this.stepHoming(p, dt);
            if (finished) done.add(p.id);
        }
        if (done.size) this.shots = this.shots.filter(p => !done.has(p.id));
    }
    stepHoming(p, dt) {
        const target = this.findEnemy(p.targetId);
        if (target) { p.toX = target.x; p.toY = target.y - this.visualY(target); }
        const dx = p.toX - p.x, dy = p.toY - p.y;
        const d = Math.hypot(dx, dy);
        const step = p.speed * dt;
        if (d > 0) p.angle = Math.atan2(dy, dx);
        if (d <= step || p.speed <= 0) {
            p.x = p.toX; p.y = p.toY; p.progress = 1;
            let dmg = p.damage;
            if (target && p.kind === P('Spike') && num(enemyDef(target.type).spitEvery) > 0) {
                dmg *= 1.5;
                this.addEffect(EffectKind.Hit, target.x, target.y, 20);
            }
            if (target && this.damageEnemy(target, dmg)) {
                if (p.slow > 0) this.applySlow(target, p.slow, p.slowTime);
                if (p.poisonDps > 0) this.applyPoison(target, p.poisonDps, p.poisonTime);
            }
            return true;
        }
        p.x += dx / d * step;
        p.y += dy / d * step;
        p.progress = clamp(1 - (d - step) / Math.max(p.startDist, d), 0, 1);
        return false;
    }
    stepMortar(p, dt) {
        p.progress = Math.min(1, p.progress + dt / D().MORTAR_FLIGHT);
        p.x = p.fromX + (p.toX - p.fromX) * p.progress;
        p.y = p.fromY + (p.toY - p.fromY) * p.progress;
        p.angle = Math.atan2(p.toY - p.fromY, p.toX - p.fromX);
        if (p.progress < 1) return false;
        this.addEffect(EffectKind.Splash, p.toX, p.toY, p.splash);
        const r2 = p.splash * p.splash;
        for (const e of this.enemies) {
            if (!e.alive) continue;
            const dx = e.x - p.toX, dy = e.y - p.toY;
            if (dx * dx + dy * dy <= r2 && !enemyDef(e.type).flying) this.damageEnemy(e, p.damage);
        }
        return true;
    }

    updateEffects(dt) {
        for (const fx of this.effects) fx.t += dt;
        this.effects = this.effects.filter(fx => fx.t < fx.ttl);
    }

    // ---------- Main step ----------
    update(dt) {
        if (this.isOver()) return;
        dt = Math.min(MAX_DT, dt);
        if (!(dt > 0)) return;
        this.time += dt;
        if (this.phase === Phase.Wave) this.updateSpawns(dt);
        this.updateBugAbilities(dt);
        this.updateEnemies(dt);
        if (this.phase === Phase.Lost) { this.updateEffects(dt); return; }
        this.updatePoison(dt);
        this.updateTowers(dt);
        this.updateShots(dt);
        this.updateSpits(dt);
        this.enemies = this.enemies.filter(e => e.alive);
        this.flushPendingBugs();
        this.updateEffects(dt);
        if (this.phase === Phase.Wave && this.spawningDone() && this.enemies.length === 0) {
            const bonus = D().WAVE_BONUS_BASE + D().WAVE_BONUS_PER * this.wave;
            this.coins += bonus;
            const harvested = this.harvest();
            const lastWave = this.wave >= this.totalWaves;
            this.phase = !lastWave ? Phase.Build : this.isLastMap() ? Phase.Won : Phase.MapCleared;
            this.spawners = [];
            this.emit('waveEnded', this.wave, bonus, harvested);
            if (this.phase === Phase.MapCleared) this.emit('mapCleared', this.mapIndex);
            if (this.phase === Phase.Won) this.emit('won');
        }
    }

    nextMap() {
        if (this.phase !== Phase.MapCleared || this.isLastMap()) return false;
        this.loadMap(this.mapIndex + 1);
        this.emit('mapStarted', this.mapIndex);
        return true;
    }
    reset(startMap = 0) {
        this.lives = D().START_LIVES;
        this.loadMap(clamp(startMap, 0, this.totalMaps - 1));
        this.emit('mapStarted', this.mapIndex);
    }
}

Game.Phase = Phase;
Game.Difficulty = Difficulty;
Game.EffectKind = EffectKind;
Game.FIRE_POINT_RADIUS = 1.5 * 64;   // 1.5 * TILE
Game.AMBUSH_BONUS = 1.3;
Game.remainder = remainder;

if (typeof module !== 'undefined') module.exports = Game;
else globalThis.Game = Game;
})();
