// Garden Guard++ web: the AI player (faithful port of autopilot.cpp). Owner: Dale.
// Uses only the public Game API, so it runs in the page (the 🤖 button) and headless in Node
// (tools/parity.js). No DOM.
//
//   const ai = new Autopilot(game);
//   ai.think();   // spend coins / focus fire; the C++ calls it every 0.5 s of game time
//   ai.step();    // convenience for the UI: call once per frame (after game.update); it thinks
//                 // every 0.5 s of game time and starts each wave after 2 s of build phase,
//                 // exactly like MainWindow::runAutopilot (cards/overlays stay the UI's job).
var Autopilot = (function () {
    'use strict';

    // data.js may define enums as frozen objects (DATA.Projectile.Mortar) or plain strings.
    function P(name) {
        return DATA.Projectile && DATA.Projectile[name] !== undefined ? DATA.Projectile[name] : name;
    }
    function env(name) {
        return typeof process !== 'undefined' && process.env ? process.env[name] : undefined;
    }

    function typeOf(id) {
        for (var i = 0; i < DATA.TOWERS.length; i++)
            if (DATA.TOWERS[i].id === id) return i;
        throw new Error('autopilot: unknown plant ' + id);
    }

    // Plants that hit bugs (as opposed to the sunflower and clover).
    function attacks(type) {
        var p = DATA.TOWERS[type].projectile;
        return p !== P('None') && p !== P('Aura');
    }

    var EARLY_PLANTS = 9;   // build this many before upgrades get priority
    var HOARD = 150;

    function Autopilot(game) {
        this.g = game;
        this.plan = [];
        this.airPlan = [];
        this.lastThink = -1;
        this.buildStart = -1;
        var custom = env('AUTOPILOT_PLAN');
        if (custom) {
            var self = this;
            custom.split(',').forEach(function (id) { self.plan.push(typeOf(id.trim())); });
            return;
        }
        var self2 = this;
        ['sprout', 'sprout', 'frost', 'mortar', 'mushroom', 'mortar', 'chili',
         'mushroom', 'cactus', 'rose', 'clover', 'mortar', 'chili', 'cactus',
         'rose', 'clover', 'corn', 'corn', 'frost', 'sprout']
            .forEach(function (id) { self2.plan.push(typeOf(id)); });
        ['sprout', 'corn', 'sprout', 'frost', 'corn', 'mushroom', 'mortar',
         'rose', 'cactus', 'corn', 'clover', 'chili', 'grape', 'mortar', 'rose',
         'cactus', 'mushroom', 'clover', 'frost', 'chili', 'sprout']
            .forEach(function (id) { self2.airPlan.push(typeOf(id)); });
    }

    // Sky-lane samples every half tile: calls fn(x, y) in tile units.
    function eachLaneSample(lanes, fn) {
        for (var l = 0; l < lanes.length; l++) {
            var lane = lanes[l];
            for (var i = 0; i + 1 < lane.length; i++) {
                var a = lane[i], b = lane[i + 1];   // [x, y] like C++ std::array, or {x, y}
                var ax = a[0] !== undefined ? a[0] : a.x, ay = a[1] !== undefined ? a[1] : a.y;
                var bx = b[0] !== undefined ? b[0] : b.x, by = b[1] !== undefined ? b[1] : b.y;
                var steps = Math.max(1, Math.trunc(Math.hypot(bx - ax, by - ay) * 2));
                for (var s = 0; s < steps; s++)
                    fn(ax + (bx - ax) * s / steps, ay + (by - ay) * s / steps);
            }
        }
    }

    function lanesOf(g) { return g.map().skyLanes || []; }

    // How good a tile is for a plant of this type (higher is better; < 0 = don't).
    Autopilot.prototype.tileScore = function (type, col, row) {
        var g = this.g, def = DATA.TOWERS[type];
        if (def.projectile === P('None')) {
            var near = 0;
            for (var r = row - 2; r <= row + 2; r++)
                for (var c = col - 2; c <= col + 2; c++) near += g.isPathTile(c, r) ? 1 : 0;
            return 100 - near;
        }
        var range = def.levels[0].range, r2 = range * range;
        if (def.projectile === P('Aura')) {
            var n = 0;
            g.towers.forEach(function (t) {
                var dc = t.col - col, dr = t.row - row;
                if (attacks(t.type) && dc * dc + dr * dr <= r2) n++;
            });
            return n > 0 ? n : -1;
        }
        var score = 0;
        if (def.projectile === P('Vine')) {
            eachLaneSample(lanesOf(g), function (x, y) {
                var dc = x - col, dr = y - row;
                if (dc * dc + dr * dr > r2) return;
                score += 1.0;
                for (var k = 0; k < g.towers.length; k++) {
                    var t = g.towers[k], p = DATA.TOWERS[t.type].projectile;
                    if (p !== P('Mortar') && p !== P('Flame')) continue;
                    var tr = DATA.TOWERS[t.type].levels[t.level].range;
                    var tc = x - t.col, trw = y - t.row;
                    if (tc * tc + trw * trw <= tr * tr) { score += 0.5; break; }
                }
            });
            return score > 0 ? score : -1;
        }
        for (var rr = 0; rr < g.rows; rr++)
            for (var cc = 0; cc < g.cols; cc++) {
                if (!g.isPathTile(cc, rr)) continue;
                var dc2 = cc - col, dr2 = rr - row;
                if (dc2 * dc2 + dr2 * dr2 <= r2) score += 1.0 + 0.15 * (g.cols - cc) / g.cols;
            }
        var ground = def.projectile === P('Mortar') || def.projectile === P('Flame');
        if (!ground) {
            eachLaneSample(lanesOf(g), function (x, y) {
                var dc = x - col, dr = y - row;
                if (dc * dc + dr * dr <= r2) score += 0.5;
            });
        }
        return score > 0 ? score : -1;
    };

    Autopilot.prototype.placeNext = function () {
        var g = this.g;
        var noLanes = lanesOf(g).length === 0;
        var fullPlan = noLanes || env('AUTOPILOT_PLAN') ? this.plan : this.airPlan;
        var plan = fullPlan.filter(function (t) {
            return g.isUnlocked(t) && !(noLanes && DATA.TOWERS[t].projectile === P('Vine'));
        });
        if (plan.length === 0) return false;
        var type = plan[g.towers.length % plan.length];
        if (g.coins < DATA.TOWERS[type].levels[0].cost) return false;
        var bestCol = -1, bestRow = -1, best = -1;
        for (var r = 0; r < g.rows; r++)
            for (var c = 0; c < g.cols; c++) {
                if (!g.canPlace(c, r)) continue;
                var s = this.tileScore(type, c, r);
                if (s > best) { best = s; bestCol = c; bestRow = r; }
            }
        if (best < 0) return false;
        return !!g.placeTower(type, bestCol, bestRow);
    };

    Autopilot.prototype.upgradeOne = function () {
        var g = this.g, pick = null, pickCost = 0;
        g.towers.forEach(function (t) {
            var cost = g.upgradeCost(t);
            if (cost < 0 || cost > g.coins) return;
            var weighted = attacks(t.type) ? cost : cost * 2;
            if (!pick || weighted < pickCost) { pick = t; pickCost = weighted; }
        });
        return !!(pick && g.upgradeTower(pick.id));
    };

    // Focus fire on high-value bugs (boss 4 > golden 3 > wild 2 > mosquito healing 2+ bugs 1).
    Autopilot.prototype.focus = function () {
        if (env('AUTOPILOT_NOFOCUS')) return;
        var g = this.g, TILE = DATA.TILE;
        function priority(e) {
            var d = DATA.ENEMIES[e.type];
            if (d.boss) return 4;
            if (d.golden) return 3;
            if (d.wild) return 2;
            if (d.healEvery > 0) {
                var near = 0, r = d.healRadius * TILE;
                g.enemies.forEach(function (o) {
                    if (o.alive && o.id !== e.id && Math.hypot(o.x - e.x, o.y - e.y) <= r) near++;
                });
                return near >= 2 ? 1 : 0;
            }
            return 0;
        }
        var target = null, best = 0;
        g.enemies.forEach(function (e) {
            if (!e.alive) return;
            var p = priority(e);
            if (p > best || (p === best && p > 0 && target && e.dist > target.dist)) { best = p; target = e; }
        });
        if (!target) return;
        var ids = [];
        g.towers.forEach(function (t) {
            if (!attacks(t.type) || t.focusId === target.id) return;
            if (t.focusId >= 0 && g.enemies.some(function (e) { return e.alive && e.id === t.focusId; }))
                return;   // already busy with another live target
            var range = DATA.TOWERS[t.type].levels[t.level].range * TILE;
            if (Math.hypot(target.x - t.x, target.y - t.y) > range || !g.canHit(t, target)) return;
            ids.push(t.id);
        });
        if (ids.length) g.focusFire(ids, target.id);
    };

    Autopilot.prototype.think = function () {
        var g = this.g;
        if (g.isOver()) return false;
        this.focus();
        var save = env('AUTOPILOT_MODE') === 'save';
        var acted = false;
        for (var i = 0; i < 32; i++) {
            var lastWave = g.wave >= g.totalWaves - 1;
            var surplus = !save && (g.coins > HOARD || lastWave);
            var did = g.towers.length < EARLY_PLANTS ? this.placeNext() || (surplus && this.upgradeOne())
                                                     : this.upgradeOne() || this.placeNext();
            if (!did) break;
            acted = true;
        }
        return acted;
    };

    // Per-frame driver, same as MainWindow::runAutopilot minus the UI cards.
    // The caller handles map-cleared (game.nextMap()) and pause. Returns true if it bought something.
    Autopilot.prototype.step = function () {
        var g = this.g;
        if (g.isOver()) return false;
        var acted = false;
        if (g.time < this.lastThink || this.lastThink < 0 || g.time - this.lastThink >= 0.5) {
            this.lastThink = g.time;
            acted = this.think();
        }
        if (g.phase === Autopilot.phase('Build')) {
            if (this.buildStart < 0 || g.time < this.buildStart) this.buildStart = g.time;
            if (g.time - this.buildStart >= 2.0) {
                g.startWave();
                this.buildStart = -1;
            }
        } else {
            this.buildStart = -1;
        }
        return acted;
    };

    // Phase enum lookup: game.js may export Game.Phase / DATA.Phase as objects, or use strings.
    Autopilot.phase = function (name) {
        var G = typeof Game !== 'undefined' ? Game : null;
        if (G && G.Phase && G.Phase[name] !== undefined) return G.Phase[name];
        if (typeof Phase !== 'undefined' && Phase[name] !== undefined) return Phase[name];
        if (DATA.Phase && DATA.Phase[name] !== undefined) return DATA.Phase[name];
        return name;
    };

    Autopilot.attacks = attacks;
    return Autopilot;
})();

if (typeof module !== 'undefined') module.exports = Autopilot;
