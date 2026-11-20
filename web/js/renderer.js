// Garden Guard++ web: all board drawing (owner: Bobby). Global: Renderer.
// Canvas port of renderer.cpp (+ BoardWidget's shadow, rounded clip and confetti).
// Reads the game state and never changes it.
//
//   Renderer.draw(ctx, game, view, ui)   every frame. view = View (js/view.js).
//     ui = { hover: {col,row,valid}|null, placing: -1|type, selectedId: -1|id, aimingId: -1|id,
//            group: [ids], realTime: seconds (optional, for confetti) }
//   Renderer.celebrate()                 confetti from the two bottom corners (BoardWidget::celebrate)
//
// Readability: the C++ grows markers/plants/bugs on big boards (FX_UI/FX_PLANT/FX_BUG from the
// board width). Here the same boost comes from the on-screen scale, so it is also right on
// phones and fades out as you pinch-zoom in: FX_UI = clamp(0.7 / scale, 1, 2.2).
var Renderer = (function () {
    'use strict';

    var TAU = Math.PI * 2;
    var TILE = 64;
    var W = 960, H = 640, GRID_COLS = 15, GRID_ROWS = 10;
    var FX_UI = 1, FX_PLANT = 1, FX_BUG = 1;

    var GLYPH_PAD = 1.3, KICK_TIME = 0.16, FLASH_TIME = 0.1, MORTAR_ARC = 60;
    var ENEMY_SIZE = TILE * 0.55, BOSS_SIZE = ENEMY_SIZE * 1.6, TOWER_ICON = TILE * 0.5, POT_R = TILE * 0.34;
    var FIRE_POINT_RADIUS = 1.5 * TILE, FLAME_HALF_ANGLE = 0.6;

    var EMOJI_FONT = '"Apple Color Emoji","Segoe UI Emoji","Noto Color Emoji","Segoe UI Symbol",sans-serif';
    var UI_FONT = 'system-ui,-apple-system,"Segoe UI",Roboto,"Noto Sans",sans-serif';
    var FLOWER_COLORS = ['#ffffff', '#ffd84d', '#ff8fb1', '#b98cff', '#ff9f5a'];
    var SPRING_FLOWERS = ['#ffffff', '#ffc8dd', '#ff8fb1', '#e7c6ff', '#fff1a8'];
    var SUMMER_FLOWERS = ['#ff3b3b', '#ffd21f', '#ff7a00', '#ff4fa3', '#8a5cff'];
    var AUTUMN_FLOWERS = ['#e8a33a', '#c94f2a', '#f2c14e', '#9b5de5', '#d9622b'];
    var LEAF_COLORS = ['#d9622b', '#e8a33a', '#b5432a', '#c98a2e', '#8f4a22'];

    // ---------- small helpers ----------
    function G() { return typeof window !== 'undefined' ? window : globalThis; }
    function DATA_() { return G().DATA; }
    function clamp(v, lo, hi) { return v < lo ? lo : v > hi ? hi : v; }
    function clamp01(v) { return clamp(v, 0, 1); }
    function easeOut(u) { var k = 1 - u; return 1 - k * k; }
    function fmod(a, b) { return a % b; }   // same sign rules as std::fmod
    function rgba(r, g, b, a) { return 'rgba(' + r + ',' + g + ',' + b + ',' + clamp01(a) + ')'; }
    function hexRgb(hex) {
        var h = hex.replace('#', '');
        if (h.length === 3) h = h[0] + h[0] + h[1] + h[1] + h[2] + h[2];
        var n = parseInt(h, 16);
        return [(n >> 16) & 255, (n >> 8) & 255, n & 255];
    }
    function darker(hex, f) {   // QColor::darker(150) ~ divide by 1.5
        var c = hexRgb(hex);
        return rgba(Math.round(c[0] / f), Math.round(c[1] / f), Math.round(c[2] / f), 1);
    }

    // Enum values are name strings (PLAN.txt); accept C++-order integers as a fallback.
    var ORDER = {
        Projectile: ['Bolt', 'Frost', 'Mortar', 'Spike', 'Spore', 'Flame', 'Thorn', 'Kernel', 'Aura', 'None', 'Vine'],
        EffectKind: ['Hit', 'Splash', 'Kill', 'Coin', 'Leak', 'Place', 'Slow', 'Poison', 'Flame', 'Heal', 'Hop',
                     'Split', 'Jackpot', 'Goo', 'BossDust', 'FlyerFall', 'Lasso'],
        Season: ['Spring', 'Summer', 'Autumn', 'Winter'],
        Facing: ['Left', 'Right', 'Up'],
    };
    function nm(kind, v) {
        if (typeof v === 'string') return v;
        if (typeof v === 'number') return ORDER[kind][v] || String(v);
        return ORDER[kind][0];
    }

    // Mulberry32, identical to the C++ Rng (so the garden layout is the same).
    function Rng(seed) {
        var a = seed >>> 0;
        return function () {
            a = (a + 0x6D2B79F5) >>> 0;
            var t = a;
            t = Math.imul(t ^ (t >>> 15), t | 1);
            t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
            return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
        };
    }

    // Game-state accessors (tolerant of small API differences).
    function mapOf(g) { return typeof g.map === 'function' ? g.map() : DATA_().MAPS[g.mapIndex]; }
    function seasonOf(g) { return nm('Season', mapOf(g).season); }
    function edef(e) { return DATA_().ENEMIES[e.type]; }
    function tdef(t) { return DATA_().TOWERS[t.type]; }
    function slowed(e, now) { return now < e.slowUntil && e.slowFactor > 0; }
    function poisoned(e, now) { return now < e.poisonUntil && e.poisonDps > 0; }
    function grounded(e, now) { return now < (e.groundedUntil || 0); }
    function stunned(t, now) { return now < (t.stunnedUntil || 0); }
    function visualY(g, e) {
        if (typeof g.visualY === 'function') return g.visualY(e);
        return edef(e).flying && !grounded(e, g.time) ? 16 : 0;
    }
    function terrainAt(g, c, r) { return typeof g.terrainAt === 'function' ? g.terrainAt(c, r) : -1; }
    function isPathTile(g, c, r) { return typeof g.isPathTile === 'function' ? g.isPathTile(c, r) : false; }
    function num(f, g, dflt) { return typeof f === 'function' ? f.call(g) : dflt; }
    function stunTimeOfScorpion() {
        var E = DATA_().ENEMIES;
        for (var i = 0; i < E.length; i++) if (E[i].name === 'Scorpion') return E[i].stunTime;
        return 3;
    }

    function newCanvas(w, h) {
        var c;
        if (typeof OffscreenCanvas !== 'undefined') c = new OffscreenCanvas(w, h);
        else { c = document.createElement('canvas'); c.width = w; c.height = h; }
        return c;
    }

    // ---------- drawing primitives ----------
    function pen(c, color, width, round) {
        c.strokeStyle = color;
        c.lineWidth = width;
        c.lineCap = round ? 'round' : 'square';
        c.lineJoin = round ? 'round' : 'bevel';
        c.setLineDash([]);
    }
    function fillEllipse(c, x, y, rx, ry, rot, style) {
        c.beginPath();
        c.ellipse(x, y, Math.max(0, rx), Math.max(0, ry), rot || 0, 0, TAU);
        c.fillStyle = style;
        c.fill();
    }
    function fillCircle(c, x, y, r, style) {
        if (r <= 0) return;
        c.beginPath();
        c.arc(x, y, r, 0, TAU);
        c.fillStyle = style;
        c.fill();
    }
    function strokeCircle(c, x, y, r, color, width) {
        if (r <= 0) return;
        pen(c, color, width, false);
        c.beginPath();
        c.arc(x, y, r, 0, TAU);
        c.stroke();
    }
    function strokeEllipse(c, x, y, rx, ry) {
        c.beginPath();
        c.ellipse(x, y, Math.max(0, rx), Math.max(0, ry), 0, 0, TAU);
        c.stroke();
    }
    function line(c, x0, y0, x1, y1) {
        c.beginPath();
        c.moveTo(x0, y0);
        c.lineTo(x1, y1);
        c.stroke();
    }
    function roundRectPath(c, x, y, w, h, r) {
        r = Math.min(r, w / 2, h / 2);
        c.beginPath();
        c.moveTo(x + r, y);
        c.arcTo(x + w, y, x + w, y + h, r);
        c.arcTo(x + w, y + h, x, y + h, r);
        c.arcTo(x, y + h, x, y, r);
        c.arcTo(x, y, x + w, y, r);
        c.closePath();
    }
    function rangeCircle(c, x, y, r, ok) {
        fillCircle(c, x, y, r, ok ? rgba(255, 255, 255, 0.12) : rgba(255, 70, 70, 0.1));
        pen(c, ok ? rgba(255, 255, 255, 0.8) : rgba(255, 140, 140, 0.85), 2, false);
        c.lineCap = 'butt';
        c.setLineDash([7, 6]);
        c.beginPath();
        c.arc(x, y, r, 0, TAU);
        c.stroke();
        c.setLineDash([]);
    }
    function drawLeaf(c, x, y, size, rot, color) {
        c.save();
        c.translate(x, y);
        c.rotate(rot);
        c.beginPath();
        c.moveTo(-size, 0);
        c.quadraticCurveTo(0, -size * 0.75, size, 0);
        c.quadraticCurveTo(0, size * 0.75, -size, 0);
        c.fillStyle = color;
        c.fill();
        pen(c, darker(color, 1.5), 0.7, false);
        line(c, -size * 0.85, 0, size * 0.8, 0);
        c.restore();
    }
    function drawSnowPatch(c, x, y, rx, ry) {
        var s = c.createRadialGradient(0, 0, 0, 0, 0, rx);
        s.addColorStop(0, rgba(252, 253, 255, 0.92));
        s.addColorStop(0.65, rgba(238, 245, 255, 0.75));
        s.addColorStop(1, rgba(238, 245, 255, 0));
        c.save();
        c.translate(x, y);
        c.scale(1, ry / rx);
        fillCircle(c, 0, 0, rx, s);
        c.restore();
    }
    function drawBlossom(c, x, y, r, rot, color) {
        for (var k = 0; k < 5; k++) {
            var a = rot + k * TAU / 5;
            fillCircle(c, x + Math.cos(a) * r, y + Math.sin(a) * r, r * 0.85, color);
        }
        fillCircle(c, x, y, r * 0.55, '#ffe27a');
    }

    // ---------- caches (glyphs, tints, pots, background) ----------
    var res_ = 0;          // device px per logical px the glyph/pot caches are built for
    var glyphs = {};
    var pots = {};
    var bg = null, bgKey = '', bgRes = 0, bgWant = 0, bgWantSince = 0;

    function glyph(ch, size, res) {
        res = res || res_;
        var key = ch + '|' + size + '|' + res;
        var pm = glyphs[key];
        if (pm) return pm;
        var px = Math.max(1, Math.ceil(size * GLYPH_PAD * res));
        pm = newCanvas(px, px);
        var g = pm.getContext('2d');
        g.font = Math.max(1, Math.round(size * res)) + 'px ' + EMOJI_FONT;
        g.textAlign = 'center';
        g.textBaseline = 'middle';
        g.fillStyle = '#000';
        g.fillText(ch, px / 2, px / 2 + size * res * 0.06);
        glyphs[key] = pm;
        return pm;
    }
    function tint(ch, size, color) {
        var key = ch + '|' + size + '|' + res_ + '|' + color;
        var pm = glyphs[key];
        if (pm) return pm;
        var src = glyph(ch, size);
        pm = newCanvas(src.width, src.height);
        var g = pm.getContext('2d');
        g.drawImage(src, 0, 0);
        g.globalCompositeOperation = 'source-atop';
        g.fillStyle = color;
        g.fillRect(0, 0, pm.width, pm.height);
        glyphs[key] = pm;
        return pm;
    }
    function drawGlyph(c, ch, size, x, y, s, res) {
        s = s === undefined ? 1 : s;
        var box = size * GLYPH_PAD * s;
        c.drawImage(glyph(ch, size, res), x - box / 2, y - box / 2, box, box);
    }
    function pot(type) {
        var key = type + '|' + res_;
        if (pots[key]) return pots[key];
        var px = Math.ceil(TILE * res_);
        var pm = newCanvas(px, px);
        var g = pm.getContext('2d');
        g.scale(res_, res_);
        var cx = TILE / 2, cy = TILE / 2, r = POT_R;
        fillEllipse(g, cx, cy + r * 0.6, r * 1.05, r * 0.45, 0, rgba(0, 0, 0, 0.22));
        var body = g.createRadialGradient(cx - r * 0.35, cy - r * 0.4, 2, cx, cy, r);
        body.addColorStop(0, '#f0a274');
        body.addColorStop(1, '#b25a31');
        fillCircle(g, cx, cy, r, body);
        strokeCircle(g, cx, cy, r, '#7a3a1c', 2);
        strokeCircle(g, cx, cy, r - 4, DATA_().TOWERS[type].color, 3);
        var soil = g.createRadialGradient(cx, cy - 2, 1, cx, cy, r - 6);
        soil.addColorStop(0, '#6e482a');
        soil.addColorStop(1, '#3d2615');
        fillCircle(g, cx, cy, r - 6.5, soil);
        fillEllipse(g, cx - r * 0.55, cy - r * 0.55, r * 0.22, r * 0.12, -0.8, rgba(255, 255, 255, 0.28));
        pots[key] = pm;
        return pm;
    }

    // Background resolution steps: rebuilt only when the zoom crosses a step (and has settled).
    var BG_STEPS = [0.5, 0.75, 1, 1.5, 2, 3, 4, 6];
    function bgStepFor(devScale) {
        var maxByPixels = Math.sqrt(16e6 / (W * H));   // <= ~16 Mpx (phones' canvas limits)
        var want = Math.min(devScale, maxByPixels);
        for (var i = 0; i < BG_STEPS.length; i++) if (BG_STEPS[i] >= want - 1e-6) return Math.min(BG_STEPS[i], maxByPixels);
        return Math.min(BG_STEPS[BG_STEPS.length - 1], maxByPixels);
    }

    function setupScale(game, scale, dpr, now) {
        W = game.W; H = game.H; GRID_COLS = game.cols; GRID_ROWS = game.rows;
        FX_UI = clamp(0.7 / scale, 1, 2.2);
        FX_PLANT = Math.min(FX_UI, 0.8 / (2 * POT_R / TILE));
        FX_BUG = Math.min(FX_UI, 0.75 / (ENEMY_SIZE / TILE));
        // Sprite caches: device px per logical px (incl. the readability boost), in 0.5 steps.
        var want = clamp(Math.ceil(scale * dpr * FX_UI * 2) / 2, 0.5, 8);
        if (want !== res_) { res_ = want; glyphs = {}; pots = {}; }
        // Background.
        var key = game.mapIndex + '|' + W + 'x' + H;
        var step = bgStepFor(scale * dpr);
        if (step !== bgWant) { bgWant = step; bgWantSince = now; }
        // Rebuild now for a new map or a big jump (first real size after layout, rotation);
        // during a pinch wait until the zoom settles.
        var jump = step > bgRes * 1.9 || step < bgRes / 1.9;
        if (!bg || key !== bgKey || (step !== bgRes && (jump || now - bgWantSince > 0.25))) {
            bgKey = key;
            bgRes = step;
            bg = buildBackground(game, step);
        }
    }

    // ---------- background ----------
    function buildBackground(game, res) {
        var D = DATA_();
        var map = mapOf(game);
        var pm = newCanvas(Math.ceil(W * res), Math.ceil(H * res));
        var g = pm.getContext('2d');
        g.scale(res, res);
        var rand = Rng(map.seed >>> 0);
        var r, c, i, k;

        // Grass tiles with subtle variation.
        for (r = 0; r < GRID_ROWS; r++) {
            for (c = 0; c < GRID_COLS; c++) {
                g.fillStyle = (r + c) % 2 ? map.grassA : map.grassB;
                g.fillRect(c * TILE, r * TILE, TILE, TILE);
                var v = rand();
                g.fillStyle = v < 0.5 ? rgba(255, 255, 220, (0.5 - v) * 0.12) : rgba(0, 50, 0, (v - 0.5) * 0.1);
                g.fillRect(c * TILE, r * TILE, TILE, TILE);
            }
        }
        // Grass blades.
        for (i = 0; i < 1100; i++) {
            var x = rand() * W, y = rand() * H;
            var shade = rand();
            var lean = (rand() - 0.5) * 3;
            var len = 3 + rand() * 4;
            pen(g, shade < 0.5 ? rgba(40, 110, 40, 0.35) : rgba(190, 240, 140, 0.35), 1.2, true);
            line(g, x, y, x + lean, y - len);
        }

        // Season wash.
        var season = nm('Season', map.season);
        var srand = Rng((map.seed ^ 0x5EA5011) >>> 0);
        if (season === 'Summer') { g.fillStyle = rgba(255, 225, 90, 0.07); g.fillRect(0, 0, W, H); }
        else if (season === 'Autumn') { g.fillStyle = rgba(225, 130, 35, 0.08); g.fillRect(0, 0, W, H); }
        else if (season === 'Winter') {
            g.fillStyle = rgba(215, 228, 245, 0.22);
            g.fillRect(0, 0, W, H);
            for (i = 0; i < GRID_COLS * GRID_ROWS * 5; i++) {
                var fx = srand() * W, fy = srand() * H, fr = 0.6 + srand() * 0.9;
                fillCircle(g, fx, fy, fr, rgba(255, 255, 255, 0.35 + srand() * 0.3));
            }
        }

        // Terrain: ponds (merged blobs, icy in winter), lily pads, boulders.
        (function () {
            var trand = Rng((map.seed ^ 0x7E44A1D) >>> 0);
            var icy = season === 'Winter';
            var WATER = D.TERRAIN_WATER, LILY = D.TERRAIN_LILY, ROCK = D.TERRAIN_ROCK;
            function isPond(cc, rr) { var kk = terrainAt(game, cc, rr); return kk === WATER || kk === LILY; }
            var rects = [], lilies = [], rocks = [];
            for (var r = 0; r < game.rows; r++)
                for (var c = 0; c < game.cols; c++) {
                    var kind = terrainAt(game, c, r);
                    var ctr = { x: c * TILE + TILE / 2, y: r * TILE + TILE / 2 };
                    if (kind === WATER || kind === LILY) {
                        var x0 = c * TILE + 8, y0 = r * TILE + 8;
                        rects.push([x0, y0, TILE - 16, TILE - 16]);
                        if (isPond(c + 1, r)) rects.push([x0, y0, TILE, TILE - 16]);
                        if (isPond(c, r + 1)) rects.push([x0, y0, TILE - 16, TILE]);
                        if (isPond(c + 1, r) && isPond(c, r + 1) && isPond(c + 1, r + 1)) rects.push([x0, y0, TILE, TILE]);
                        if (kind === LILY) lilies.push(ctr);
                    } else if (kind === ROCK) rocks.push(ctr);
                }
            if (rects.length) {
                var bx0 = 1e9, by0 = 1e9, bx1 = -1e9, by1 = -1e9;
                var blob = new Path2D(), grown = new Path2D();
                rects.forEach(function (q) {
                    blob.rect(q[0], q[1], q[2], q[3]);
                    // The blob grown by 8 px with round corners (the area the water stroke covers).
                    var gx = q[0] - 8, gy = q[1] - 8, gw = q[2] + 16, gh = q[3] + 16, rr = 8;
                    grown.moveTo(gx + rr, gy);
                    grown.arcTo(gx + gw, gy, gx + gw, gy + gh, rr);
                    grown.arcTo(gx + gw, gy + gh, gx, gy + gh, rr);
                    grown.arcTo(gx, gy + gh, gx, gy, rr);
                    grown.arcTo(gx, gy, gx + gw, gy, rr);
                    grown.closePath();
                    bx0 = Math.min(bx0, q[0]); by0 = Math.min(by0, q[1]);
                    bx1 = Math.max(bx1, q[0] + q[2]); by1 = Math.max(by1, q[1] + q[3]);
                });
                bx0 -= 12; by0 -= 12; bx1 += 12; by1 += 12;
                var bw = bx1 - bx0, bh = by1 - by0;
                var water = g.createLinearGradient(bx0, by0, bx1, by1);
                water.addColorStop(0, icy ? '#e4f2fa' : '#5fb3d9');
                water.addColorStop(1, icy ? '#bcd9ea' : '#3b8fbf');
                pen(g, icy ? '#8fb4c8' : '#3d7f6a', 22, true);
                g.stroke(blob);
                pen(g, water, 16, true);
                g.stroke(blob);
                g.fillStyle = water;
                g.fill(blob);
                g.save();
                g.clip(grown);
                var n = Math.floor(bw * bh / 1400);
                for (var i = 0; i < n; i++) {
                    var x = bx0 + trand() * bw, y = by0 + trand() * bh;
                    if (icy) {
                        pen(g, rgba(255, 255, 255, 0.7), 1.5, true);
                        var dx = 10 + trand() * 12, dy = 4 + trand() * 4;
                        line(g, x, y, x + dx, y - dy);
                    } else {
                        pen(g, rgba(220, 245, 255, 0.55), 1.4, true);
                        g.beginPath();   // Qt arc 20..160 deg counter-clockwise (y up)
                        g.ellipse(x, y + 0.5, 9, 3.5, 0, -20 * Math.PI / 180, -160 * Math.PI / 180, true);
                        g.stroke();
                    }
                }
                g.restore();
            }
            lilies.forEach(function (p) {
                var R = TILE * 0.36, rot = trand() * 360;
                var a0 = -rot * Math.PI / 180, a1 = -(rot + 320) * Math.PI / 180;
                g.beginPath();
                g.moveTo(p.x, p.y);
                g.arc(p.x, p.y, R, a0, a1, true);
                g.closePath();
                g.fillStyle = icy ? '#7fae7a' : '#58b04a';
                g.fill();
                pen(g, '#2f7a2f', 1.5, false);
                g.stroke();
                pen(g, rgba(30, 90, 30, 0.5), 1.2, true);
                var a = (rot + 160) * Math.PI / 180;
                line(g, p.x, p.y, p.x + Math.cos(a) * R * 0.8, p.y - Math.sin(a) * R * 0.8);
                if (!icy && trand() < 0.5) {
                    var f = { x: p.x + R * 0.35, y: p.y - R * 0.3 };
                    for (var k = 0; k < 5; k++) {
                        var pa = k * TAU / 5;
                        fillCircle(g, f.x + Math.cos(pa) * 3.2, f.y + Math.sin(pa) * 3.2, 2.8, '#f7a8c8');
                    }
                    fillCircle(g, f.x, f.y, 2, '#ffe066');
                }
            });
            rocks.forEach(function (p) {
                fillEllipse(g, p.x + 3, p.y + 12, TILE * 0.38, TILE * 0.14, 0, rgba(0, 0, 0, 0.22));
                var n = 2 + Math.floor(trand() * 2);
                for (var k = 0; k < n; k++) {
                    var ox = (k - (n - 1) / 2) * 13 + (trand() - 0.5) * 6;
                    var oy = (trand() - 0.5) * 8 + (k === 1 ? -4 : 2);
                    var rx = 13 + trand() * 6 - k * 1.5, ry = 10 + trand() * 4 - k;
                    var sh = 120 + Math.floor(trand() * 40);
                    fillEllipse(g, p.x + ox, p.y + oy, rx, ry, 0, icy ? rgba(sh + 50, sh + 58, sh + 68, 1) : rgba(sh, sh, sh + 6, 1));
                    pen(g, 'rgb(70,70,75)', 1.4, false);
                    strokeEllipse(g, p.x + ox, p.y + oy, rx, ry);
                    fillEllipse(g, p.x + ox - rx * 0.3, p.y + oy - ry * 0.4, rx * 0.35, ry * 0.25, 0,
                                rgba(255, 255, 255, icy ? 0.75 : 0.4));
                }
            });
        })();

        // Dirt path (+ second entrance).
        var pathPts = game.path, path2 = game.path2 || [];
        var route = new Path2D();
        route.moveTo(pathPts[0].x, pathPts[0].y);
        for (i = 1; i < pathPts.length; i++) route.lineTo(pathPts[i].x, pathPts[i].y);
        if (path2.length >= 2) {
            route.moveTo(path2[0].x, path2[0].y);
            for (i = 1; i < path2.length; i++) route.lineTo(path2[i].x, path2[i].y);
        }
        if (season === 'Winter') { pen(g, rgba(250, 252, 255, 0.75), TILE * 1.0, true); g.stroke(route); }
        [[0.92, rgba(50, 35, 15, 0.22)], [0.84, '#93653b'], [0.74, '#c99a62'], [0.40, rgba(228, 190, 135, 0.45)]]
            .forEach(function (wc) { pen(g, wc[1], TILE * wc[0], true); g.stroke(route); });

        // Dirt specks and pebbles along each path.
        [pathPts, path2].forEach(function (pts) {
            if (pts.length < 2) return;
            var segLen = [], total = 0;
            for (var i = 0; i + 1 < pts.length; i++) {
                var l = Math.hypot(pts[i + 1].x - pts[i].x, pts[i + 1].y - pts[i].y);
                segLen.push(l);
                total += l;
            }
            for (i = 0; i < 520; i++) {
                var d = rand() * total, s = 0;
                while (s < segLen.length - 1 && d > segLen[s]) { d -= segLen[s]; s++; }
                var a = pts[s], b = pts[s + 1];
                var L = segLen[s] > 0 ? segLen[s] : 1, f = segLen[s] > 0 ? d / segLen[s] : 0;
                var nx = -(b.y - a.y) / L, ny = (b.x - a.x) / L;
                var off = (rand() - 0.5) * TILE * 0.6;
                var x = a.x + (b.x - a.x) * f + nx * off, y = a.y + (b.y - a.y) * f + ny * off;
                var kind = rand();
                if (kind < 0.08) {
                    var rx = 2.5 + rand() * 2, ry = 1.8 + rand() * 1.2, rot = rand() * 3;
                    fillEllipse(g, x, y, rx, ry, rot, '#a49c90');
                } else {
                    var col = kind < 0.55 ? rgba(120, 80, 40, 0.45) : rgba(235, 205, 160, 0.5);
                    fillCircle(g, x, y, 0.7 + rand() * 1.5, col);
                }
            }
        });

        // Decorations on grass tiles (rand() order matches the C++ exactly).
        for (r = 0; r < GRID_ROWS; r++) {
            for (c = 0; c < GRID_COLS; c++) {
                var roll = rand();
                var cx = c * TILE + 12 + rand() * (TILE - 24);
                var cy = r * TILE + 12 + rand() * (TILE - 24);
                if (isPathTile(game, c, r) || terrainAt(game, c, r) >= 0) continue;
                if (roll < 0.17 && season === 'Winter') {
                    var srx = TILE * (0.18 + rand() * 0.14), sry = TILE * (0.1 + rand() * 0.06);
                    drawSnowPatch(g, cx, cy, srx, sry);
                } else if (roll < 0.17) {
                    var palette = season === 'Spring' ? SPRING_FLOWERS : season === 'Summer' ? SUMMER_FLOWERS
                                : season === 'Autumn' ? AUTUMN_FLOWERS : FLOWER_COLORS;
                    var n = 1 + Math.floor(rand() * 3);
                    for (i = 0; i < n; i++) {
                        var px = cx + (rand() - 0.5) * 14, py = cy + (rand() - 0.5) * 14;
                        var color = palette[Math.floor(rand() * 5)];
                        var frot = rand() * TAU;
                        pen(g, rgba(40, 110, 40, 0.6), 1.2, true);
                        line(g, px, py, px, py + 5);
                        for (k = 0; k < 5; k++) {
                            var a = frot + k * TAU / 5;
                            fillCircle(g, px + Math.cos(a) * 2.8, py + Math.sin(a) * 2.8, 2.3, color);
                        }
                        fillCircle(g, px, py, 1.7, '#ffb627');
                    }
                } else if (roll < 0.27) {
                    var np = 2 + Math.floor(rand() * 2);
                    for (i = 0; i < np; i++) {
                        var qx = cx + (rand() - 0.5) * 14, qy = cy + (rand() - 0.5) * 10;
                        var prx = 2.5 + rand() * 2.5;
                        var prot = rand() * 3;
                        fillEllipse(g, qx, qy, prx, prx * 0.7, prot, '#b3ada3');
                        pen(g, '#7d776f', 0.8, false);
                        g.beginPath(); g.ellipse(qx, qy, prx, prx * 0.7, prot, 0, TAU); g.stroke();
                        fillCircle(g, qx - prx * 0.3, qy - prx * 0.25, prx * 0.3, rgba(255, 255, 255, 0.35));
                    }
                } else if (roll < 0.45) {
                    pen(g, season === 'Winter' ? '#a9c9b4' : season === 'Autumn' ? '#a58a3a' : '#4f9a3b', 1.6, true);
                    for (k = 0; k < 5; k++) {
                        var ba = -Math.PI / 2 + (k - 2) * 0.32 + (rand() - 0.5) * 0.2;
                        var bl = 6 + rand() * 4;
                        g.beginPath();
                        g.moveTo(cx, cy + 4);
                        g.quadraticCurveTo(cx + Math.cos(ba) * bl * 0.5, cy + 4 + Math.sin(ba) * bl * 0.6,
                                           cx + Math.cos(ba) * bl, cy + 4 + Math.sin(ba) * bl);
                        g.stroke();
                    }
                }
            }
        }

        // Season scatter: blossoms, leaf piles or snow drifts.
        if (season !== 'Summer') {
            for (r = 0; r < GRID_ROWS; r++) {
                for (c = 0; c < GRID_COLS; c++) {
                    var sroll = srand();
                    var scx = c * TILE + TILE * (0.15 + srand() * 0.7);
                    var scy = r * TILE + TILE * (0.15 + srand() * 0.7);
                    var onPath = isPathTile(game, c, r) || terrainAt(game, c, r) >= 0;
                    if (season === 'Spring') {
                        if (onPath || sroll > 0.16) continue;
                        var nb = 3 + Math.floor(srand() * 4);
                        for (i = 0; i < nb; i++) {
                            var bxp = scx + (srand() - 0.5) * TILE * 0.4, byp = scy + (srand() - 0.5) * TILE * 0.3;
                            var brot = srand() * TAU;
                            drawBlossom(g, bxp, byp, TILE * 0.03, brot, srand() < 0.6 ? '#ffc2d6' : '#fff4f8');
                        }
                    } else if (season === 'Autumn') {
                        if (onPath ? sroll > 0.08 : sroll > 0.2) continue;
                        var nl = onPath ? 1 : 2 + Math.floor(srand() * (sroll < 0.12 ? 6 : 2));
                        for (i = 0; i < nl; i++) {
                            var lx = scx + (srand() - 0.5) * TILE * 0.35, ly = scy + (srand() - 0.5) * TILE * 0.3;
                            var lsz = TILE * (0.06 + srand() * 0.03), lrot = srand() * TAU;
                            drawLeaf(g, lx, ly, lsz, lrot, LEAF_COLORS[Math.floor(srand() * 5)]);
                        }
                    } else {
                        if (onPath || sroll > 0.22) continue;
                        var wrx = TILE * (0.22 + srand() * 0.2), wry = TILE * (0.12 + srand() * 0.08);
                        drawSnowPatch(g, scx, scy, wrx, wry);
                    }
                }
            }
        }

        // Bug nest just inside the edge where each entrance's path comes in.
        [pathPts, path2].forEach(function (pts) {
            if (pts.length < 2) return;
            var first = pts[0], second = pts[1];
            var dir = second.x > first.x ? 1 : second.x < first.x ? -1 : 0;
            var dirY = dir !== 0 ? 0 : second.y > first.y ? 1 : second.y < first.y ? -1 : 0;
            var nx = clamp(first.x + dir * TILE * 1.3, TILE * 0.32, W - TILE * 0.32);
            var ny = clamp(first.y + dirY * TILE * 1.3, TILE * 0.32, H - TILE * 0.32);
            fillEllipse(g, nx, ny, 30, 24, 0, '#7a5432');
            var hole = g.createRadialGradient(nx, ny + 2, 2, nx, ny, 24);
            hole.addColorStop(0, '#120a05');
            hole.addColorStop(0.7, '#2e1d10');
            hole.addColorStop(1, '#5a3a20');
            fillEllipse(g, nx, ny, 23, 18, 0, hole);
            for (var k = 0; k < 16; k++) {
                var a = k * TAU / 16;
                var x = nx + Math.cos(a) * 27, y = ny + Math.sin(a) * 21;
                pen(g, k % 2 ? '#8a5f36' : '#5e3f22', 2.4, true);
                line(g, x - Math.sin(a) * 6, y + Math.cos(a) * 5, x + Math.sin(a) * 6, y - Math.cos(a) * 5);
            }
        });

        // House at the path's exit.
        var last = pathPts[pathPts.length - 1];
        var hx = Math.min(W - TILE * 0.72, last.x), hy = last.y - 6;
        fillEllipse(g, hx, hy + 24, 36, 11, 0, rgba(0, 0, 0, 0.2));
        fillEllipse(g, hx, hy + 20, 32, 9, 0, '#d9c7a0');
        drawGlyph(g, '🏡', 56, hx, hy, 1, res);

        drawSkyLanes(g, game, map, season);

        if (map.night) {
            g.fillStyle = rgba(10, 20, 60, 0.38);
            g.fillRect(0, 0, W, H);
            var glow = g.createRadialGradient(W - 70, 60, 0, W - 70, 60, 260);
            glow.addColorStop(0, rgba(220, 230, 255, 0.28));
            glow.addColorStop(1, rgba(220, 230, 255, 0));
            g.fillStyle = glow;
            g.fillRect(0, 0, W, H);
            drawGlyph(g, '🌙', 40, W - 70, 60, 1, res);
        }

        // Soft vignette.
        var vg = g.createRadialGradient(W / 2, H / 2, H * 0.45, W / 2, H / 2, W * 0.68);
        vg.addColorStop(0, rgba(20, 40, 10, 0));
        vg.addColorStop(1, map.night ? rgba(0, 0, 20, 0.45) : season === 'Winter' ? rgba(40, 60, 90, 0.22)
                         : season === 'Autumn' ? rgba(70, 35, 5, 0.24) : rgba(20, 40, 10, 0.22));
        g.fillStyle = vg;
        g.fillRect(0, 0, W, H);
        return pm;
    }

    // Sky lanes: faint dashed trails with chevrons toward the house (into the background).
    function drawSkyLanes(g, game, map, season) {
        var winter = season === 'Winter';
        // Lanes live in the cached background: use the C++ board-width boost, not the zoom-aware one.
        var FX_UI = clamp(Math.sqrt(W / 960), 1, 1.5);
        (map.skyLanes || []).forEach(function (lane) {
            if (lane.length < 2) return;
            var pts = lane.map(function (t) {
                var tx = t.x !== undefined ? t.x : t[0], ty = t.y !== undefined ? t.y : t[1];
                return { x: tx * TILE + TILE / 2, y: ty * TILE + TILE / 2 };
            });
            var trail = new Path2D();
            trail.moveTo(pts[0].x, pts[0].y);
            for (var i = 1; i < pts.length; i++) trail.lineTo(pts[i].x, pts[i].y);
            pen(g, rgba(180, 225, 255, 0.18), 14 * FX_UI, true);
            g.stroke(trail);
            pen(g, winter ? rgba(60, 100, 160, 0.6) : rgba(255, 255, 255, 0.55), 2.5 * FX_UI, true);
            g.setLineDash([7.5 * FX_UI, 7.5 * FX_UI]);
            g.stroke(trail);
            g.setLineDash([]);
            // Chevrons every 1.5 tiles.
            var segs = [], total = 0;
            for (i = 0; i + 1 < pts.length; i++) {
                var l = Math.hypot(pts[i + 1].x - pts[i].x, pts[i + 1].y - pts[i].y);
                segs.push(l);
                total += l;
            }
            pen(g, winter ? rgba(50, 90, 150, 0.75) : rgba(255, 255, 255, 0.7), 2.2 * FX_UI, true);
            for (var d = TILE; d < total - TILE * 0.5; d += TILE * 1.5) {
                var s = 0, rem = d;
                while (s < segs.length - 1 && rem > segs[s]) { rem -= segs[s]; s++; }
                var A = pts[s], B = pts[s + 1], L = segs[s] || 1;
                var ca = (B.x - A.x) / L, sa = (B.y - A.y) / L;
                var ax = A.x + ca * rem, ay = A.y + sa * rem;
                var tipx = ax + ca * 5, tipy = ay + sa * 5, backx = ax - ca * 3, backy = ay - sa * 3;
                var sx = -sa * 5, sy = ca * 5;
                line(g, backx + sx, backy + sy, tipx, tipy);
                line(g, backx - sx, backy - sy, tipx, tipy);
            }
        });
    }

    // ---------- per-frame layers ----------
    function drawFireflies(p, g) {
        if (!mapOf(g).night) return;
        var t = g.time;
        for (var i = 0; i < 28; i++) {
            var phase = i * 2.39996;
            var x = fmod(80 + i * 137.0 + 40 * Math.sin(t * 0.31 + phase) + t * (6 + i % 5), W);
            var y = fmod(40 + i * 89.0 + 30 * Math.cos(t * 0.27 + phase * 1.3), H);
            if (y < 0) y += H;
            var blink = 0.5 + 0.5 * Math.sin(t * (1.6 + (i % 7) * 0.23) + phase);
            if (blink < 0.15) continue;
            var glow = p.createRadialGradient(x, y, 0, x, y, 9);
            glow.addColorStop(0, rgba(255, 250, 170, 0.75 * blink));
            glow.addColorStop(0.35, rgba(220, 255, 120, 0.35 * blink));
            glow.addColorStop(1, rgba(200, 255, 100, 0));
            fillCircle(p, x, y, 9, glow);
        }
    }

    function drawWeather(p, g) {
        var season = seasonOf(g);
        if (season === 'Summer') return;
        var t = g.time;
        var area = (W * H) / (960.0 * 640.0);
        var i, phase, x, y, r;
        p.save();
        if (season === 'Winter') {
            var n = Math.floor(60 * area);
            for (i = 0; i < n; i++) {
                phase = i * 2.39996;
                var fall = TILE * (0.3 + (i % 5) * 0.08);
                x = fmod(i * 97.0 + TILE * 0.25 * Math.sin(t * 0.7 + phase) + t * TILE * 0.1, W);
                y = fmod(i * 53.0 + t * fall, H + 10) - 5;
                if (x < 0) x += W;
                r = TILE * (0.025 + (i % 3) * 0.012);
                fillCircle(p, x, y, r, rgba(255, 255, 255, 0.5 + 0.12 * (i % 3)));
            }
        } else {
            var autumn = season === 'Autumn';
            var m = Math.floor((autumn ? 14 : 8) * area);
            var pad = TILE * 0.5;
            p.globalAlpha = 0.85;
            for (i = 0; i < m; i++) {
                phase = i * 2.39996;
                x = fmod(i * 173.0 + t * TILE * (0.3 + (i % 4) * 0.07) + TILE * 0.5 * Math.sin(t * 0.9 + phase), W + 2 * pad) - pad;
                y = fmod(i * 71.0 + t * TILE * (0.2 + (i % 3) * 0.06), H + 2 * pad) - pad;
                if (x < -pad) x += W + 2 * pad;
                var rot = t * (1.2 + (i % 5) * 0.3) + phase;
                if (autumn) drawLeaf(p, x, y, TILE * (0.08 + (i % 3) * 0.015), rot, LEAF_COLORS[i % 5]);
                else {
                    var flip = 0.35 + 0.65 * Math.abs(Math.sin(rot * 1.3));
                    fillEllipse(p, x, y, TILE * 0.05, TILE * 0.032 * flip, rot, i % 2 ? '#ffc2d6' : '#fff0f5');
                }
            }
        }
        p.restore();
    }

    function drawGrid(p) {
        pen(p, rgba(255, 255, 255, 0.16), 1, false);
        p.lineCap = 'butt';
        p.beginPath();
        for (var c = 1; c < GRID_COLS; c++) { p.moveTo(c * TILE, 0); p.lineTo(c * TILE, H); }
        for (var r = 1; r < GRID_ROWS; r++) { p.moveTo(0, r * TILE); p.lineTo(W, r * TILE); }
        p.stroke();
    }

    function drawPreview(p, ui) {
        var h = ui.hover;
        if (h.col < 0 || h.col >= GRID_COLS || h.row < 0 || h.row >= GRID_ROWS) return;
        var def = DATA_().TOWERS[ui.placing];
        var x = h.col * TILE, y = h.row * TILE, cx = x + TILE / 2, cy = y + TILE / 2;
        roundRectPath(p, x + 3, y + 3, TILE - 6, TILE - 6, 10);
        p.fillStyle = h.valid ? rgba(130, 255, 130, 0.3) : rgba(255, 80, 80, 0.32);
        p.fill();
        pen(p, h.valid ? rgba(220, 255, 220, 0.95) : rgba(255, 180, 180, 0.95), 2, false);
        p.stroke();
        if (def.levels[0].range > 0) rangeCircle(p, cx, cy, def.levels[0].range * TILE, h.valid);
        p.globalAlpha = 0.6;
        p.drawImage(pot(ui.placing), x, y, TILE, TILE);
        drawGlyph(p, def.icon, TOWER_ICON, cx, cy - 2);
        p.globalAlpha = 1;
    }

    function drawRanges(p, g, ui) {
        if (ui.selectedId < 0) return;
        for (var i = 0; i < g.towers.length; i++) {
            var t = g.towers[i];
            if (t.id !== ui.selectedId) continue;
            var range = tdef(t).levels[t.level].range;
            if (range > 0) rangeCircle(p, t.x, t.y, range * TILE, true);
            break;
        }
    }

    function drawTowers(p, g, ui) {
        var showHover = ui.placing < 0 && !!ui.hover;
        var now = g.time;
        g.towers.forEach(function (t) {
            var def = tdef(t);
            var proj = nm('Projectile', def.projectile);
            var cx = t.x, cy = t.y, ca = Math.cos(t.angle || 0), sa = Math.sin(t.angle || 0);
            p.save();
            p.translate(cx, cy);
            p.scale(FX_PLANT, FX_PLANT);
            p.translate(-cx, -cy);

            if (t.id === ui.selectedId) {
                strokeCircle(p, cx, cy, POT_R + 6, rgba(255, 240, 140, 0.35), 7);
                strokeCircle(p, cx, cy, POT_R + 6, '#fff1a0', 3);
            } else if (showHover && ui.hover.col === t.col && ui.hover.row === t.row) {
                strokeCircle(p, cx, cy, POT_R + 5, rgba(255, 255, 255, 0.7), 2);
            }
            p.drawImage(pot(t.type), cx - TILE / 2, cy - TILE / 2, TILE, TILE);

            var attacks = proj !== 'None' && proj !== 'Aura';
            var scale = 1, ox = 0, oy = 0, i;
            if (proj === 'Aura' && !stunned(t, now)) {
                var R = def.levels[t.level].range * TILE / FX_PLANT;
                var ph = fmod(now * 0.6 + t.id * 0.37, 1.0);
                strokeCircle(p, cx, cy, R * ph, rgba(90, 220, 130, 0.45 * (1 - ph)), 2.5);
                strokeCircle(p, cx, cy, R, rgba(90, 220, 130, 0.18), 1.5);
            }
            if (t.level >= 2) {
                for (i = 0; i < 3; i++) {
                    var a = now * 1.8 + i * TAU / 3 + t.id;
                    var sx = cx + Math.cos(a) * (POT_R + 7), sy = cy + Math.sin(a) * (POT_R + 4);
                    var tw = 0.6 + 0.4 * Math.sin(now * 7 + i * 2.1);
                    fillCircle(p, sx, sy, 3.2 * tw, rgba(255, 225, 90, 0.35));
                    fillCircle(p, sx, sy, 1.6 * tw, rgba(255, 250, 200, 0.95));
                }
            }
            if (attacks) {
                var ax = cx + ca * (POT_R + 1), ay = cy + sa * (POT_R + 1);
                p.beginPath();
                p.moveTo(ax + ca * 6, ay + sa * 6);
                p.lineTo(ax - ca * 3 - sa * 4.5, ay - sa * 3 + ca * 4.5);
                p.lineTo(ax - ca * 3 + sa * 4.5, ay - sa * 3 - ca * 4.5);
                p.closePath();
                p.fillStyle = def.color;
                p.fill();
                pen(p, rgba(255, 255, 255, 0.9), 1.2, false);
                p.lineJoin = 'miter';
                p.stroke();
            } else {
                scale = 1 + 0.05 * Math.sin(now * 2.2 + t.id);
                oy = 1.5 * Math.sin(now * 1.7 + t.id * 0.7);
            }
            var k = now - t.shotTime;
            if (t.shotTime >= 0 && k >= 0 && k < KICK_TIME) {
                var f = 1 - k / KICK_TIME;
                scale = 1 + 0.22 * f;
                ox = -ca * 3 * f;
                oy = -sa * 3 * f;
            }
            drawGlyph(p, def.icon, TOWER_ICON, cx + ox, cy - 2 + oy, scale);

            if (stunned(t, now)) {
                var left = clamp01((t.stunnedUntil - now) / Math.max(0.1, stunTimeOfScorpion()));
                fillCircle(p, cx, cy, POT_R + 1, rgba(60, 66, 60, 0.6));
                var bx = cx - 9, by = cy - 11;
                p.beginPath();
                p.ellipse(bx, by, 8, 6, 0, 0, TAU);
                p.moveTo(bx - 1 + 0, by + 4);
                p.ellipse(bx - 5, by + 4, 4, 4, 0, 0, TAU);
                p.moveTo(bx + 10, by + 3);
                p.ellipse(bx + 6, by + 3, 4, 3.5, 0, 0, TAU);
                p.fillStyle = '#8fdc3c';
                p.fill('nonzero');
                pen(p, '#2f6b12', 1.5, false);
                p.stroke();
                p.fill('nonzero');   // hide inner outlines (Qt draws the simplified union)
                var drip = 3 + 5 * fmod(now * 1.5 + t.id, 1.0);
                fillCircle(p, bx - 4, by + 7 + drip, 2.2, '#8fdc3c');
                fillCircle(p, bx - 2, by - 2, 1.8, rgba(255, 255, 255, 0.8));
                pen(p, rgba(20, 40, 10, 0.55), 5, true);
                p.beginPath(); p.arc(cx, cy, POT_R + 4, 0, TAU); p.stroke();
                pen(p, '#b6ff6a', 3, true);
                p.beginPath(); p.arc(cx, cy, POT_R + 4, -Math.PI / 2, -Math.PI / 2 - left * TAU, true); p.stroke();
            }

            var n = clamp(t.level + 1, 1, 3), py = cy + TILE * 0.4;
            for (i = 0; i < n; i++) {
                var pxx = cx + (i - (n - 1) / 2) * 8;
                fillCircle(p, pxx, py, 2.7, '#ffd84d');
                strokeCircle(p, pxx, py, 2.7, '#6b4a12', 1);
            }
            p.restore();
        });
    }

    function drawEnemies(p, g) {
        var time = g.time;
        var order = g.enemies.filter(function (e) { return !edef(e).flying; })
            .concat(g.enemies.filter(function (e) { return edef(e).flying; }));
        order.forEach(function (e) {
            var def = edef(e);
            var size = (def.boss ? BOSS_SIZE : ENEMY_SIZE) * (def.scale || 1) * FX_BUG;
            var isSlow = slowed(e, time), isPois = poisoned(e, time);
            var bob = Math.sin(time * (isSlow ? 8 : 14) + e.id * 1.7);
            var isBat = def.name === 'Bat';
            var facing = nm('Facing', def.facing);
            var alt = 0, side = 0;
            if (def.flying) {
                alt = visualY(g, e);
                if (isBat) alt += 5 * Math.sin(time * 2.4 + e.id);
                else side = 6 * Math.sin(time * 3.5 + e.id);
            }
            var gx = e.x - Math.sin(e.angle) * side, gy = e.y + Math.cos(e.angle) * side;
            if (def.flying) fillEllipse(p, gx, gy + size * 0.3, size * 0.26, size * 0.09, 0, rgba(0, 0, 0, 0.16));
            else fillEllipse(p, e.x, e.y + size * 0.3, size * 0.38, size * 0.14, 0, rgba(0, 0, 0, 0.2));
            if (isSlow) { pen(p, rgba(170, 230, 255, 0.95), 2, false); strokeEllipse(p, gx, gy + size * 0.3, size * 0.5, size * 0.2); }
            if (isPois) { pen(p, rgba(150, 230, 80, 0.9), 2, false); strokeEllipse(p, gx, gy + size * 0.3, size * 0.58, size * 0.24); }

            var tr = e.trail || [];
            if (def.wild && tr.length > 1) {
                for (var i = 1; i < tr.length; i++) {
                    var f = i / tr.length;
                    pen(p, rgba(190, 120, 255, 0.28 * f), 1 + 3 * f, true);
                    line(p, tr[i - 1].x, tr[i - 1].y - alt, tr[i].x, tr[i].y - alt);
                }
            }
            if (grounded(e, time) && e.vineBy >= 0) {
                for (var ti = 0; ti < g.towers.length; ti++) {
                    var t = g.towers[ti];
                    if (t.id !== e.vineBy) continue;
                    var vx = e.x - t.x, vy = e.y - (t.y - 6), len = Math.hypot(vx, vy);
                    var nx = len > 0 ? -vy / len : 0, ny = len > 0 ? vx / len : 0;
                    var vine = new Path2D();
                    vine.moveTo(t.x, t.y - 6);
                    for (var k = 1; k <= 12; k++) {
                        var q = k / 12, wob = Math.sin(q * 3 * Math.PI + time * 6) * 4 * (1 - q * 0.5);
                        vine.lineTo(t.x + vx * q + nx * wob, t.y - 6 + vy * q + ny * wob);
                    }
                    pen(p, '#2f6b1f', 3.5, true); p.stroke(vine);
                    pen(p, '#6cbf3a', 1.8, true); p.stroke(vine);
                    fillEllipse(p, t.x + vx * 0.5 + nx * 5, t.y - 6 + vy * 0.5 + ny * 5, 4, 2.2, Math.atan2(vy, vx), '#5fae35');
                    pen(p, '#2f6b1f', 2, true);
                    strokeEllipse(p, e.x, e.y, size * 0.32, size * 0.16);
                    break;
                }
            }

            p.save();
            var hopK = time - e.hopStart;
            var lift = (e.hopStart >= 0 && hopK >= 0 && hopK < 0.35 ? Math.sin(Math.PI * hopK / 0.35) * 20 : 0) + alt;
            if (def.flying) {
                var halo = p.createRadialGradient(gx, gy - lift, 0, gx, gy - lift, size * 0.62);
                halo.addColorStop(0, rgba(255, 252, 225, 0.42));
                halo.addColorStop(1, rgba(255, 252, 225, 0));
                fillCircle(p, gx, gy - lift, size * 0.62, halo);
                p.translate(gx, gy - lift);
                var flap = Math.abs(Math.cos(time * (isBat ? 9 : 14) + e.id));
                p.scale(0.72 + 0.28 * flap, 1);
            } else if (facing === 'Up') {
                p.translate(e.x, e.y + bob * 0.9 - lift);
                p.rotate(e.angle + Math.PI / 2 + bob * 0.07);
            } else {
                p.translate(e.x, e.y + bob * 0.9 - lift);
                p.rotate(e.angle + bob * 0.07);
                if (Math.cos(e.angle) < 0) p.scale(1, -1);
                if (facing === 'Left') p.scale(-1, 1);
            }
            var box = size * GLYPH_PAD;
            var put = function (img, alpha) { p.globalAlpha = alpha; p.drawImage(img, -box / 2, -box / 2, box, box); };
            put(glyph(def.icon, size), 1);
            if (isSlow) put(tint(def.icon, size, '#7fd3ff'), 0.42);
            if (isPois) put(tint(def.icon, size, '#8be04e'), 0.3 + 0.12 * Math.sin(time * 9 + e.id));
            if (def.golden) put(tint(def.icon, size, '#ffcc33'), 0.55 + 0.2 * Math.sin(time * 7 + e.id));
            if (def.spitEvery > 0 && e.abilityTimer > 0 && e.abilityTimer < 0.45)
                put(tint(def.icon, size, '#9cff4a'), 0.65 * (1 - e.abilityTimer / 0.45));
            var hk = time - e.healTime;
            if (e.healTime >= 0 && hk >= 0 && hk < 0.4) put(tint(def.icon, size, '#7dffa8'), 0.6 * (1 - hk / 0.4));
            var fk = time - e.hitTime;
            if (e.hitTime >= 0 && fk >= 0 && fk < FLASH_TIME) put(tint(def.icon, size, '#ffffff'), 0.8 * (1 - fk / FLASH_TIME));
            if (def.wild) {
                p.globalAlpha = 1;
                for (var s = -1; s <= 1; s += 2) {
                    fillCircle(p, s * size * 0.075, -size * 0.13, size * 0.07, rgba(255, 40, 40, 0.35));
                    fillCircle(p, s * size * 0.075, -size * 0.13, size * 0.032, '#ff3b3b');
                }
            }
            p.globalAlpha = 1;
            p.restore();

            if (def.golden) {
                pen(p, '#fff3a0', 1.6, true);
                for (var sp = 0; sp < 4; sp++) {
                    var ang = time * 2.2 + sp * TAU / 4 + e.id;
                    var twk = 0.5 + 0.5 * Math.sin(time * 9 + sp * 2.1);
                    var sx2 = e.x + Math.cos(ang) * size * 0.55, sy2 = e.y + Math.sin(ang) * size * 0.4 - lift;
                    var rr = 2 + 3 * twk;
                    p.globalAlpha = 0.4 + 0.6 * twk;
                    line(p, sx2 - rr, sy2, sx2 + rr, sy2);
                    line(p, sx2, sy2 - rr, sx2, sy2 + rr);
                }
                p.globalAlpha = 1;
            }

            if (e.hp < e.maxHp && e.maxHp > 0) {
                var ratio = clamp01(e.hp / e.maxHp);
                var bw = size * 0.85, bh = (def.boss ? 5 : 4) * FX_UI;
                var bx = gx - bw / 2, by = gy - alt - size * 0.62 - bh;
                p.fillStyle = rgba(20, 20, 20, 0.65);
                p.fillRect(bx - 1, by - 1, bw + 2, bh + 2);
                p.fillStyle = ratio > 0.5 ? '#5ee35e' : ratio > 0.25 ? '#ffd24d' : '#ff5a4d';
                p.fillRect(bx, by, bw * ratio, bh);
            }
        });

        (g.spits || []).forEach(function (s) {
            var arc = Math.sin(Math.PI * s.progress) * 26;
            fillEllipse(p, s.x, s.y + 4, 5, 2, 0, rgba(0, 0, 0, 0.22));
            for (var k = 1; k <= 3; k++) {
                var q = Math.max(0, s.progress - k * 0.07);
                var tx = s.fromX + (s.toX - s.fromX) * q, ty = s.fromY + (s.toY - s.fromY) * q;
                fillCircle(p, tx, ty - Math.sin(Math.PI * q) * 26, 3.2 - k * 0.7, rgba(120, 200, 60, 0.6 - k * 0.15));
            }
            fillEllipse(p, s.x, s.y - arc, 5.5, 5, 0, '#8fdc3c');
            pen(p, '#2f6b12', 1.5, false);
            strokeEllipse(p, s.x, s.y - arc, 5.5, 5);
            fillCircle(p, s.x - 1.8, s.y - arc - 1.8, 1.6, rgba(255, 255, 255, 0.7));
        });
    }

    function polygon(p, pts, fill, strokeColor, width) {
        p.beginPath();
        p.moveTo(pts[0], pts[1]);
        for (var i = 2; i < pts.length; i += 2) p.lineTo(pts[i], pts[i + 1]);
        p.closePath();
        if (fill) { p.fillStyle = fill; p.fill(); }
        if (strokeColor) { pen(p, strokeColor, width, false); p.lineJoin = 'miter'; p.stroke(); }
    }
    function disc(p, x, y, r, fill, strokeColor, width) {
        fillCircle(p, x, y, r, fill);
        if (strokeColor) strokeCircle(p, x, y, r, strokeColor, width);
    }

    function drawShots(p, g) {
        g.shots.forEach(function (s) {
            var ca = Math.cos(s.angle || 0), sa = Math.sin(s.angle || 0);
            switch (nm('Projectile', s.kind)) {
            case 'Bolt':
                disc(p, s.x, s.y, 4.5, '#72d84a', '#2f7d1f', 1.5);
                fillCircle(p, s.x - 1.4, s.y - 1.4, 1.4, rgba(255, 255, 255, 0.75));
                break;
            case 'Frost':
                pen(p, rgba(180, 235, 255, 0.5), 4, true);
                line(p, s.x - ca * 16, s.y - sa * 16, s.x, s.y);
                polygon(p, [s.x + ca * 7, s.y + sa * 7, s.x - sa * 4.5, s.y + ca * 4.5,
                            s.x - ca * 7, s.y - sa * 7, s.x + sa * 4.5, s.y - ca * 4.5], '#e6f8ff', '#3fa9d6', 1.5);
                break;
            case 'Spike':
                pen(p, '#3b3a2a', 2.4, true);
                line(p, s.x - ca * 9, s.y - sa * 9, s.x + ca * 6, s.y + sa * 6);
                polygon(p, [s.x + ca * 11, s.y + sa * 11, s.x + ca * 5 - sa * 2.2, s.y + sa * 5 + ca * 2.2,
                            s.x + ca * 5 + sa * 2.2, s.y + sa * 5 - ca * 2.2], '#26261b');
                break;
            case 'Mortar': {
                var pr = clamp01(s.progress);
                var gx = s.fromX + (s.toX - s.fromX) * pr, gy = s.fromY + (s.toY - s.fromY) * pr;
                var lift = Math.sin(pr * Math.PI) * MORTAR_ARC, hf = lift / MORTAR_ARC;
                fillEllipse(p, gx, gy, 8 - 3.5 * hf, 3.2 - 1.4 * hf, 0, rgba(0, 0, 0, 0.32 - 0.16 * hf));
                disc(p, gx, gy - lift, 7, '#f0883e', '#9b4a12', 1.6);
                fillCircle(p, gx - 2.2, gy - lift - 2.4, 2.2, rgba(255, 255, 255, 0.55));
                break;
            }
            case 'Spore': {
                var wob = Math.sin(g.time * 18 + s.id) * 0.8;
                fillCircle(p, s.x, s.y, 8 + wob, rgba(190, 120, 230, 0.25));
                disc(p, s.x, s.y, 4.5, '#c08ae6', '#6d2f91', 1.2);
                fillCircle(p, s.x - ca * 6 - 2, s.y - sa * 6 + 1, 1.6, rgba(200, 150, 240, 0.6));
                fillCircle(p, s.x - ca * 9 + 2, s.y - sa * 9 - 1, 1.1, rgba(200, 150, 240, 0.4));
                break;
            }
            case 'Thorn':
                fillCircle(p, s.x - ca * 12, s.y - sa * 12, 2.4, rgba(240, 120, 160, 0.35));
                fillCircle(p, s.x - ca * 19, s.y - sa * 19, 1.6, rgba(240, 120, 160, 0.2));
                pen(p, '#4c7a2a', 2.6, true);
                line(p, s.x - ca * 8, s.y - sa * 8, s.x + ca * 4, s.y + sa * 4);
                polygon(p, [s.x + ca * 10, s.y + sa * 10, s.x + ca * 3 - sa * 3, s.y + sa * 3 + ca * 3,
                            s.x + ca * 3 + sa * 3, s.y + sa * 3 - ca * 3], '#d6336c', '#8a1c3f', 1);
                break;
            case 'Kernel': {
                var spin = g.time * 14 + s.id;
                for (var i = 0; i < 3; i++) {
                    var a = spin + i * 2.1;
                    disc(p, s.x + Math.cos(a) * 2.4, s.y + Math.sin(a) * 2.4, 3, '#fff6d6', '#c89b1c', 1);
                }
                fillCircle(p, s.x, s.y, 1.8, '#f5c518');
                break;
            }
            default: break;   // Flame (an effect), Aura, None, Vine (a tether)
            }
        });
    }

    function outlinedText(p, text, x, y, px, strokeColor, strokeW, fillColor, baseline) {
        p.font = 'bold ' + px + 'px ' + UI_FONT;
        p.textAlign = 'center';
        p.textBaseline = baseline;
        p.lineJoin = 'round';
        p.lineCap = 'round';
        p.lineWidth = strokeW;
        p.strokeStyle = strokeColor;
        p.setLineDash([]);
        p.strokeText(text, x, y);
        p.fillStyle = fillColor;
        p.fillText(text, x, y);
    }

    function drawWind(p, g) {
        var ws = num(g.windStrength, g, 0), ww = num(g.windWarning, g, 0);
        if (!(ws > 0.01 || ww > 0)) return;
        var dir = num(g.windDir, g, 1);
        p.save();
        p.globalAlpha = Math.min(1, 0.3 * ww + 0.9 * ws);
        var n = Math.max(8, Math.floor(15 * Math.sqrt(g.W * g.H / (960.0 * 640.0))));
        var slant = 0.22;
        for (var i = 0; i < n; i++) {
            var speed = (1000 + (i * 37 % 400)) * FX_UI;
            var len = (110 + (i * 53 % 70)) * FX_UI;
            var travel = fmod(g.time * speed + i * 397.3, g.H + len * 2) - len;
            var y = dir > 0 ? travel : g.H - travel;
            var x = fmod(i * 157.7 + 40 + travel * slant, g.W);
            var sway = Math.sin(g.time * 2.2 + i * 1.7) * 10 * FX_UI;
            var tx = x - len * slant, ty = y - dir * len;
            var path = function (o) {
                p.beginPath();
                p.moveTo(tx + o, ty + o);
                p.bezierCurveTo(tx + sway + o, y - dir * len * 0.66 + o, x - sway + o, y - dir * len * 0.33 + o, x + o, y + o);
            };
            var shade = p.createLinearGradient(tx, ty, x, y);
            shade.addColorStop(0, rgba(20, 60, 30, 0));
            shade.addColorStop(1, rgba(20, 60, 30, 0.2));
            pen(p, shade, 3.4 * FX_UI, true);
            path(1.2);
            p.stroke();
            var fade = p.createLinearGradient(tx, ty, x, y);
            fade.addColorStop(0, rgba(255, 255, 255, 0));
            fade.addColorStop(0.6, rgba(255, 255, 255, 0.55));
            fade.addColorStop(1, rgba(255, 255, 255, 0.95));
            pen(p, fade, 2.4 * FX_UI, true);
            path(0);
            p.stroke();
        }
        p.restore();
    }

    function drawEffects(p, g) {
        drawWind(p, g);
        g.effects.forEach(function (fx) {
            var u = fx.ttl > 0 ? clamp01(fx.t / fx.ttl) : 1, a = 1 - u, eu = easeOut(u);
            var kind = nm('EffectKind', fx.kind);
            var k, ang, d, r, R;
            p.save();
            var marker = kind === 'Hit' || kind === 'Kill' || kind === 'Coin' || kind === 'Jackpot' || kind === 'Split';
            if (marker && FX_UI > 1) {
                p.translate(fx.x, fx.y);
                p.scale(FX_UI, FX_UI);
                p.translate(-fx.x, -fx.y);
            }
            switch (kind) {
            case 'Hit': {
                p.globalAlpha = a;
                pen(p, '#fff6c8', 2, true);
                var r0 = 3 + 7 * eu, r1 = r0 + 2 + 5 * a;
                for (k = 0; k < 6; k++) {
                    ang = k * TAU / 6 + 0.3;
                    line(p, fx.x + Math.cos(ang) * r0, fx.y + Math.sin(ang) * r0, fx.x + Math.cos(ang) * r1, fx.y + Math.sin(ang) * r1);
                }
                break;
            }
            case 'Splash':
                R = (fx.radius > 0 ? fx.radius : TILE) * (0.35 + 0.65 * eu);
                p.globalAlpha = 0.4 * a;
                fillCircle(p, fx.x, fx.y, R, '#ffb35c');
                p.globalAlpha = a;
                strokeCircle(p, fx.x, fx.y, R, '#e0681c', 1 + 3 * a);
                for (k = 0; k < 8; k++) {
                    ang = k * TAU / 8 + 0.2;
                    fillCircle(p, fx.x + Math.cos(ang) * R * 0.8, fx.y + Math.sin(ang) * R * 0.8 - 10 * Math.sin(u * Math.PI), 2, '#7a5030');
                }
                break;
            case 'Kill':
                p.globalAlpha = 0.85 * a;
                d = 4 + 14 * eu; r = 6 * (1 - u * 0.5);
                for (k = 0; k < 5; k++) {
                    ang = k * TAU / 5 + 0.3;
                    fillCircle(p, fx.x + Math.cos(ang) * d, fx.y + Math.sin(ang) * d, r, '#f4f4f4');
                }
                break;
            case 'Coin':
                p.globalAlpha = u < 0.7 ? 1 : (1 - u) / 0.3;
                outlinedText(p, fx.text || '', fx.x, fx.y - 10 - 26 * eu, 15, '#5a3b00', 3.5, '#ffd84d', 'middle');
                break;
            case 'Leak':
                r = 10 + 36 * eu;
                p.globalAlpha = 0.25 * a;
                fillCircle(p, fx.x, fx.y, r, '#ff4d4d');
                p.globalAlpha = a;
                strokeCircle(p, fx.x, fx.y, r, '#ff3b3b', 1 + 4 * a);
                break;
            case 'Place':
                p.globalAlpha = a;
                strokeCircle(p, fx.x, fx.y, 10 + 26 * eu, '#c4ff9a', 3);
                break;
            case 'Slow': {
                p.globalAlpha = a;
                pen(p, '#e2f7ff', 1.5, true);
                d = 8 + 10 * eu;
                var sz = 1 + 3 * a;
                for (k = 0; k < 4; k++) {
                    ang = k * TAU / 4 + u * 1.5;
                    var x = fx.x + Math.cos(ang) * d, y = fx.y + Math.sin(ang) * d;
                    line(p, x - sz, y, x + sz, y);
                    line(p, x, y - sz, x, y + sz);
                }
                break;
            }
            case 'Poison':
                p.globalAlpha = a;
                for (k = 0; k < 4; k++) {
                    var bx = fx.x + (k - 1.5) * 6 + Math.sin(u * 6 + k) * 2;
                    var by = fx.y - 4 - (10 + k * 3) * eu;
                    disc(p, bx, by, 1.5 + (k % 2) * 1.2, '#9be05a', '#3f7d1a', 1);
                }
                break;
            case 'Flame': {
                R = fx.radius * (0.55 + 0.45 * eu);
                var half = (DATA_().FLAME_HALF_ANGLE || FLAME_HALF_ANGLE), fa = fx.angle || 0;
                var fire = p.createRadialGradient(fx.x, fx.y, 0, fx.x, fx.y, Math.max(0.01, R));
                fire.addColorStop(0, rgba(255, 245, 160, 0.95));
                fire.addColorStop(0.45, rgba(255, 150, 40, 0.85));
                fire.addColorStop(1, rgba(220, 40, 20, 0));
                p.globalAlpha = a;
                p.beginPath();
                p.moveTo(fx.x, fx.y);
                p.arc(fx.x, fx.y, Math.max(0, R), fa - half, fa + half);
                p.closePath();
                p.fillStyle = fire;
                p.fill();
                break;
            }
            case 'Heal':
                R = fx.radius * (0.3 + 0.7 * eu);
                p.globalAlpha = 0.18 * a;
                fillCircle(p, fx.x, fx.y, R, '#7dffa8');
                p.globalAlpha = 0.8 * a;
                strokeCircle(p, fx.x, fx.y, R, '#3ccf6e', 2);
                pen(p, '#2fbf5f', 2.2, true);
                for (k = 0; k < 3; k++) {
                    var hx = fx.x + (k - 1) * 12, hy = fx.y - 10 - 18 * eu - (k % 2) * 6;
                    line(p, hx - 3.5, hy, hx + 3.5, hy);
                    line(p, hx, hy - 3.5, hx, hy + 3.5);
                }
                break;
            case 'Hop':
                p.globalAlpha = 0.6 * a;
                for (k = 0; k < 5; k++) {
                    ang = Math.PI + (k - 2) * 0.45;
                    d = 4 + 12 * eu;
                    fillCircle(p, fx.x + Math.cos(ang) * d * (k % 2 ? 1 : -1), fx.y + 8 - 4 * eu + Math.sin(ang) * 2,
                               3.5 * (1 - u * 0.5), '#c9b48a');
                }
                break;
            case 'Split':
                p.globalAlpha = 0.7 * a;
                fillCircle(p, fx.x, fx.y, fx.radius * (0.4 + 0.3 * eu), '#8fd14f');
                for (k = 0; k < 7; k++) {
                    ang = k * TAU / 7 + 0.4;
                    d = fx.radius * (0.3 + 1.1 * eu);
                    fillCircle(p, fx.x + Math.cos(ang) * d, fx.y + Math.sin(ang) * d, 3 * (1 - u * 0.6), '#5fa82e');
                }
                break;
            case 'Jackpot': {
                p.globalAlpha = a;
                pen(p, '#ffd84d', 3, true);
                var j0 = 8 + 20 * eu, j1 = j0 + 10 * a;
                for (k = 0; k < 12; k++) {
                    ang = k * TAU / 12 + u;
                    line(p, fx.x + Math.cos(ang) * j0, fx.y + Math.sin(ang) * j0, fx.x + Math.cos(ang) * j1, fx.y + Math.sin(ang) * j1);
                }
                for (k = 0; k < 8; k++) {
                    ang = k * TAU / 8 + 0.2;
                    d = 10 + 34 * eu;
                    var cy = fx.y + Math.sin(ang) * d - 30 * Math.sin(u * Math.PI) + 20 * u * u;
                    disc(p, fx.x + Math.cos(ang) * d, cy, 4, '#ffd84d', '#6b4a12', 1);
                }
                p.globalAlpha = u < 0.75 ? 1 : (1 - u) / 0.25;
                outlinedText(p, fx.text || '', fx.x, fx.y - 30 - 30 * eu, 20, '#5a3b00', 4, '#ffe066', 'alphabetic');
                break;
            }
            case 'Lasso':
                p.globalAlpha = a;
                pen(p, '#5fae35', 3, true);
                R = fx.radius * (1.6 - 0.9 * eu);
                strokeEllipse(p, fx.x, fx.y, R, R * 0.6);
                break;
            case 'FlyerFall': {
                var drop = fx.radius * u * u + 6 * u;
                p.save();
                p.globalAlpha = a;
                p.translate(fx.x + 6 * Math.sin(u * 5), fx.y + drop);
                p.rotate(u * 2.4 * Math.PI);
                var fsz = ENEMY_SIZE * (1 - 0.45 * u);
                if (fx.text) p.drawImage(glyph(fx.text, Math.round(fsz * 4) / 4), -fsz * GLYPH_PAD / 2, -fsz * GLYPH_PAD / 2, fsz * GLYPH_PAD, fsz * GLYPH_PAD);
                p.restore();
                p.globalAlpha = clamp01(0.5 * u * a * 2);
                fillCircle(p, fx.x, fx.y + fx.radius + ENEMY_SIZE * 0.3, 6 * u, rgba(200, 190, 160, 0.6));
                break;
            }
            case 'BossDust':
                p.globalAlpha = 0.55 * a;
                strokeCircle(p, fx.x, fx.y, fx.radius * (0.3 + 0.9 * eu), '#d9c49a', 3 * a + 1);
                for (k = 0; k < 9; k++) {
                    ang = k * TAU / 9 + 0.2;
                    d = fx.radius * (0.2 + 0.8 * eu);
                    fillCircle(p, fx.x + Math.cos(ang) * d, fx.y + Math.sin(ang) * d * 0.6 - 6 * eu, 6 + 6 * eu, rgba(200, 180, 140, 0.7));
                }
                break;
            case 'Goo':
                p.globalAlpha = 0.85 * a;
                fillCircle(p, fx.x, fx.y, fx.radius * (0.5 + 0.4 * eu), rgba(140, 220, 60, 0.5));
                for (k = 0; k < 8; k++) {
                    ang = k * TAU / 8 + 0.3;
                    d = fx.radius * (0.4 + 0.9 * eu);
                    fillCircle(p, fx.x + Math.cos(ang) * d, fx.y + Math.sin(ang) * d, 3.2 * (1 - u * 0.5), '#6cbf2a');
                }
                break;
            default: break;
            }
            p.restore();
        });
        p.globalAlpha = 1;
    }

    function drawFirePoints(p, g, ui) {
        var FPR = (G().Game && G().Game.FIRE_POINT_RADIUS) || FIRE_POINT_RADIUS;
        function reticle(x, y, rgb, alpha) {
            var col = rgba(rgb[0], rgb[1], rgb[2], alpha);
            pen(p, col, 2 * FX_UI, false);
            p.lineCap = 'butt';
            p.setLineDash([6 * FX_UI, 4 * FX_UI]);
            p.beginPath(); p.arc(x, y, FPR, 0, TAU); p.stroke();
            p.setLineDash([]);
            p.beginPath(); p.arc(x, y, 7 * FX_UI, 0, TAU); p.stroke();
            for (var k = 0; k < 4; k++) {
                var a = k * Math.PI / 2;
                line(p, x + Math.cos(a) * 10 * FX_UI, y + Math.sin(a) * 10 * FX_UI, x + Math.cos(a) * 16 * FX_UI, y + Math.sin(a) * 16 * FX_UI);
            }
        }
        var group = ui.group || [];
        g.towers.forEach(function (t) {
            if (!t.hasFirePoint) return;
            var focus = t.id === ui.selectedId || t.id === ui.aimingId;
            if (focus) {
                pen(p, rgba(255, 90, 70, 0.8), 2, false);
                p.lineCap = 'butt';
                p.setLineDash([8, 6]);
                line(p, t.x, t.y, t.fireX, t.fireY);
                p.setLineDash([]);
            }
            reticle(t.fireX, t.fireY, [255, 90, 70], focus ? 0.95 : 0.5);
        });
        g.towers.forEach(function (t) {
            if (group.indexOf(t.id) < 0 || t.id === ui.selectedId) return;
            var gr = (TILE * 0.34 + 6) * FX_PLANT;
            strokeCircle(p, t.x, t.y, gr, rgba(255, 240, 140, 0.35), 6 * FX_PLANT);
            strokeCircle(p, t.x, t.y, gr, '#fff1a0', 2 * FX_PLANT);
        });
        var marked = {};
        g.towers.forEach(function (t) {
            if (t.focusId === undefined || t.focusId === null || t.focusId < 0) return;
            g.enemies.forEach(function (e) {
                if (e.id !== t.focusId || !e.alive) return;
                var ey = e.y - visualY(g, e);
                pen(p, rgba(255, 70, 50, 0.45), 1.5, false);
                p.lineCap = 'butt';
                p.setLineDash([4.5, 4.5]);
                line(p, t.x, t.y, e.x, ey);
                p.setLineDash([]);
                if (!marked[e.id]) {
                    marked[e.id] = true;
                    var pulse = 1 + 0.15 * Math.sin(g.time * 10);
                    var r = TILE * 0.42 * pulse * FX_BUG;
                    pen(p, rgba(255, 60, 40, 0.95), 2.5 * FX_UI, false);
                    p.beginPath(); p.arc(e.x, ey, r, 0, TAU); p.stroke();
                    for (var k = 0; k < 4; k++) {
                        var a = k * Math.PI / 2 + Math.PI / 4;
                        line(p, e.x + Math.cos(a) * r * 0.7, ey + Math.sin(a) * r * 0.7, e.x + Math.cos(a) * r * 1.25, ey + Math.sin(a) * r * 1.25);
                    }
                }
            });
        });
        if (ui.aimingId >= 0 && ui.hover) {
            g.towers.forEach(function (t) {
                if (t.id !== ui.aimingId) return;
                var x = ui.hover.col * TILE + TILE / 2, y = ui.hover.row * TILE + TILE / 2;
                var range = tdef(t).levels[t.level].range * TILE;
                var ok = Math.hypot(x - t.x, y - t.y) <= range;
                reticle(x, y, ok ? [125, 255, 125] : [255, 107, 107], 0.95);
            });
        }
    }

    // Boss HP bar, drawn in screen space (CSS px) at the bottom of the visible board,
    // so it stays put and readable when zoomed in.
    function drawBossBar(p, g, rect) {
        var bosses = g.enemies.filter(function (e) { return e.alive && edef(e).boss; });
        if (!bosses.length) return;
        bosses.sort(function (a, b) { return a.id - b.id; });
        var k = Math.min(1, (rect.w - 24) / 360);
        var bw = 340, bh = 12, gap = 6;
        p.save();
        p.translate(rect.x + rect.w / 2, rect.y + rect.h);
        p.scale(k, k);
        var x0 = -bw / 2, y0 = -22;
        p.globalAlpha = 1;
        roundRectPath(p, x0 - 10, y0 - 22, bw + 20, bh + 30, 9);
        p.fillStyle = rgba(30, 22, 14, 0.62);
        p.fill();
        var bd = edef(bosses[0]);
        var name = bd.icon + '  ' + bd.name + (bosses.length > 1 ? ' ×' + bosses.length : '');
        p.font = 'bold 13px ' + UI_FONT;
        p.textAlign = 'center';
        p.textBaseline = 'middle';
        p.fillStyle = '#ffe7a8';
        p.fillText(name, 0, y0 - 11);
        var n = bosses.length, segW = (bw - gap * (n - 1)) / n;
        bosses.forEach(function (e, i) {
            var sx = x0 + i * (segW + gap);
            var ratio = clamp01(e.hp / Math.max(1, e.maxHp));
            p.fillStyle = rgba(10, 8, 6, 0.85);
            p.fillRect(sx - 1, y0 - 1, segW + 2, bh + 2);
            var grad = p.createLinearGradient(sx, y0, sx, y0 + bh);
            grad.addColorStop(0, '#ff6a4d');
            grad.addColorStop(1, '#c22d1e');
            p.fillStyle = grad;
            p.fillRect(sx, y0, segW * ratio, bh);
            pen(p, '#e8b84a', 1.2, false);
            p.lineJoin = 'miter';
            p.strokeRect(sx - 1, y0 - 1, segW + 2, bh + 2);
            var hk = g.time - e.hitTime;
            if (e.hitTime >= 0 && hk >= 0 && hk < 0.12) {
                p.globalAlpha = 0.5 * (1 - hk / 0.12);
                p.fillStyle = '#ffffff';
                p.fillRect(sx, y0, segW * ratio, bh);
                p.globalAlpha = 1;
            }
        });
        p.restore();
    }

    // Touch loupe: while a finger is down placing a plant or picking a fire point, the finger hides
    // the tile, so show a bubble above it (screen space, constant size) with the plant / 🎯 and a
    // green (ok) or red (blocked / out of range) ring, plus a pointer down to the tile.
    function drawTouchLoupe(p, g, U, view) {
        if (!U.touch || !U.hover || (U.placing < 0 && U.aimingId < 0)) return;
        var h = U.hover;
        var cx = view.ox + (h.col * TILE + TILE / 2) * view.scale;
        var cy = view.oy + (h.row * TILE + TILE / 2) * view.scale;
        var ok, icon, potType = -1;
        if (U.placing >= 0) {
            ok = !!h.valid;
            icon = DATA_().TOWERS[U.placing].icon;
            potType = U.placing;
        } else {
            var t = null;
            for (var i = 0; i < g.towers.length; i++) if (g.towers[i].id === U.aimingId) t = g.towers[i];
            if (!t) return;
            var x = h.col * TILE + TILE / 2, y = h.row * TILE + TILE / 2;
            ok = Math.hypot(x - t.x, y - t.y) <= tdef(t).levels[t.level].range * TILE;
            icon = '🎯';
        }
        var R = 30, lift = 74;
        var bx = clamp(cx, R + 4, view.cssW - R - 4);
        var by = cy - lift;
        if (by < R + 4) by = cy + lift;   // no room above (top row): show it below the finger
        var ring = ok ? '#4caf50' : '#e0533d';
        p.save();
        p.globalAlpha = 1;
        // Pointer from the bubble to the tile, and a ring around the tile itself.
        pen(p, ring, 3, true);
        line(p, bx, by + (by < cy ? R : -R), cx, cy + (by < cy ? -view.scale * TILE * 0.5 : view.scale * TILE * 0.5));
        strokeCircle(p, cx, cy, Math.max(10, view.scale * TILE * 0.55), ring, 3);
        // Bubble.
        p.shadowColor = 'rgba(0,0,0,0.3)';
        p.shadowBlur = 8;
        p.shadowOffsetY = 2;
        fillCircle(p, bx, by, R, 'rgba(255,255,255,0.96)');
        p.shadowColor = 'transparent';
        strokeCircle(p, bx, by, R, ring, 4);
        if (potType >= 0) p.drawImage(pot(potType), bx - R * 0.95, by - R * 0.95, R * 1.9, R * 1.9);
        var sz = R * (potType >= 0 ? 0.95 : 1.15);
        p.drawImage(glyph(icon, 32, 3), bx - sz * GLYPH_PAD / 2, by - 2 - sz * GLYPH_PAD / 2, sz * GLYPH_PAD, sz * GLYPH_PAD);
        // ✓ / ✕ badge.
        fillCircle(p, bx + R * 0.72, by + R * 0.72, 10, ring);
        p.font = 'bold 13px ' + UI_FONT;
        p.textAlign = 'center';
        p.textBaseline = 'middle';
        p.fillStyle = '#fff';
        p.fillText(ok ? '✓' : '✕', bx + R * 0.72, by + R * 0.72 + 0.5);
        p.restore();
    }

    // ---------- confetti (BoardWidget::celebrate) ----------
    var COLORS = ['#ff5a5f', '#ffc93c', '#4caf50', '#5bc0de', '#b06ad9', '#ff9f5a', '#ffffff'];
    var confetti = [], pendingBurst = false, lastConfetti = 0;
    function rnd(n) { return Math.floor(Math.random() * n); }
    function spawnConfetti(r) {
        for (var i = 0; i < 180; i++) {
            var left = i % 2 === 0;
            var angle = (left ? -60 : -120) + (rnd(40) - 20);
            var speed = (500 + rnd(450)) * Math.max(0.5, Math.min(1, r.h / 640));
            var rad = angle * Math.PI / 180;
            confetti.push({ x: left ? r.x + 20 : r.x + r.w - 20, y: r.y + r.h - 10,
                            vx: Math.cos(rad) * speed, vy: Math.sin(rad) * speed,
                            rot: rnd(360), spin: rnd(720) - 360, w: 5 + rnd(5), h: 8 + rnd(6), color: COLORS[rnd(7)] });
        }
    }
    function drawConfetti(p, now, cssH) {
        if (!confetti.length) { lastConfetti = now; return; }
        var dt = Math.min(0.05, Math.max(0, now - lastConfetti));
        lastConfetti = now;
        confetti.forEach(function (c) {
            c.vy += 900 * dt;
            c.vx *= 1 - 1.2 * dt;
            c.vy *= 1 - 0.8 * dt;
            c.x += c.vx * dt;
            c.y += c.vy * dt;
            c.rot += c.spin * dt;
            p.save();
            p.translate(c.x, c.y);
            p.rotate(c.rot * Math.PI / 180);
            p.scale(Math.cos(c.rot * Math.PI / 90), 1);
            p.fillStyle = c.color;
            p.fillRect(-c.w / 2, -c.h / 2, c.w, c.h);
            p.restore();
        });
        var bottom = cssH + 40;
        confetti = confetti.filter(function (c) { return !(c.y > bottom && c.vy > 0); });
    }

    // ---------- public ----------
    function draw(ctx, game, view, ui) {
        if (!ctx || !game || !game.path || !game.path.length) return;
        view = view || G().View;
        ui = ui || {};
        var U = {
            hover: ui.hover !== undefined ? ui.hover : (ui.hoverTile || null),
            placing: ui.placing !== undefined ? ui.placing : (ui.placingType !== undefined ? ui.placingType : -1),
            selectedId: ui.selectedId !== undefined ? ui.selectedId : (ui.selection !== undefined ? ui.selection : -1),
            aimingId: ui.aimingId !== undefined ? ui.aimingId : -1,
            group: ui.group || [],
            touch: !!ui.touch,   // a finger is down (shows the touch loupe while placing/aiming)
        };
        if (U.placing === null) U.placing = -1;
        if (U.selectedId === null) U.selectedId = -1;
        if (U.aimingId === null) U.aimingId = -1;
        var now = ui.realTime !== undefined ? ui.realTime
                : (typeof performance !== 'undefined' ? performance.now() / 1000 : Date.now() / 1000);

        var cv = ctx.canvas;
        // Refit when the canvas's CSS size changed without a View.resize (missed resize event,
        // phone rotation, address bar showing/hiding).
        // Before layout (display:none / 0x0 canvas) draw nothing and cache nothing.
        if (typeof cv.clientWidth === 'number' && cv.getBoundingClientRect && (cv.clientWidth < 2 || cv.clientHeight < 2)) return;
        var cw = cv.clientWidth || cv.width, ch = cv.clientHeight || cv.height;
        if (!view.cssW || (cv.clientWidth && (Math.abs(cw - view.cssW) > 0.5 || Math.abs(ch - view.cssH) > 0.5)))
            view.resize(cw, ch, (typeof window !== 'undefined' && window.devicePixelRatio) || view.dpr || 1, game);
        view.sync(game);
        var dpr = view.dpr, bw = Math.round(view.cssW * dpr), bh = Math.round(view.cssH * dpr);
        if (cv.width !== bw || cv.height !== bh) { cv.width = bw; cv.height = bh; }
        var s = view.scale;
        setupScale(game, s, dpr, now);

        ctx.setTransform(1, 0, 0, 1, 0, 0);
        ctx.globalAlpha = 1;
        ctx.clearRect(0, 0, cv.width, cv.height);
        ctx.imageSmoothingEnabled = true;
        ctx.imageSmoothingQuality = 'high';

        // Board shadow and rounded clip (CSS px).
        var rect = view.boardRect();
        ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
        roundRectPath(ctx, rect.x, rect.y + 4, rect.w, rect.h, 16);
        ctx.fillStyle = rgba(40, 70, 20, 0.12);
        ctx.fill();
        ctx.save();
        roundRectPath(ctx, rect.x, rect.y, rect.w, rect.h, 16);
        ctx.clip();

        // World layers.
        ctx.setTransform(dpr * s, 0, 0, dpr * s, dpr * view.ox, dpr * view.oy);
        ctx.drawImage(bg, 0, 0, W, H);
        if (U.placing >= 0) drawGrid(ctx);
        if (U.placing >= 0 && U.hover) drawPreview(ctx, U);
        drawRanges(ctx, game, U);
        drawFirePoints(ctx, game, U);
        drawTowers(ctx, game, U);
        drawEnemies(ctx, game);
        drawShots(ctx, game);
        drawEffects(ctx, game);
        drawWeather(ctx, game);
        drawFireflies(ctx, game);
        ctx.restore();

        // Screen-space overlays (CSS px).
        ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
        var vis = { x: Math.max(0, rect.x), y: Math.max(0, rect.y) };
        vis.w = Math.min(view.cssW, rect.x + rect.w) - vis.x;
        vis.h = Math.min(view.cssH, rect.y + rect.h) - vis.y;
        drawBossBar(ctx, game, vis);
        drawTouchLoupe(ctx, game, U, view);
        if (pendingBurst) { pendingBurst = false; spawnConfetti(vis); lastConfetti = now; }
        drawConfetti(ctx, now, view.cssH);
        ctx.globalAlpha = 1;
    }

    return {
        draw: draw,
        celebrate: function () { pendingBurst = true; },   // spawned on the next draw (needs the board rect)
        isCelebrating: function () { return confetti.length > 0 || pendingBurst; },
        // Exposed for tests/QA.
        _fx: function () { return { FX_UI: FX_UI, FX_PLANT: FX_PLANT, FX_BUG: FX_BUG, res: res_, bgRes: bgRes }; },
    };
})();

if (typeof module !== 'undefined') module.exports = Renderer;
