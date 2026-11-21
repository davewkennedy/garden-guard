// Garden Guard++ web: board camera (owner: Bobby). Global: View.
// Ports BoardWidget::boardRect / tileAt and adds pinch-zoom + pan for phones.
//
// Coordinates:
//   screen = CSS px relative to the board canvas's top-left (what pointer events give,
//            after subtracting canvas.getBoundingClientRect()).
//   world  = logical board px (TILE = 64), exactly as in the C++ game.
//   screen = world * View.scale + (View.ox, View.oy)
// Device pixels: the canvas backing store is cssW*dpr x cssH*dpr; Renderer.draw applies dpr,
// so everything stays crisp on phones.
//
// Immersive layout: the canvas fills the whole viewport (the garden is drawn past the board),
// and the board fits inside a "safe rect" (the #boardWrap placeholder, clear of the header and
// side panel). Without a safe rect the whole canvas is used.
//
// API (Hank's input.js / main.js call these):
//   View.resize(cssW, cssH, dpr, game)  fit the whole map; keeps zoom/center on a plain resize,
//                                       refits when the map size changes
//   View.setSafeRect(x, y, w, h)        where the board should fit, in canvas CSS px (call on
//                                       every resize; no args = the full canvas)
//   View.zoomAt(sx, sy, factor)         zoom around a screen point (pinch center / mouse wheel)
//   View.panBy(dx, dy)                  move the board by screen px (1-finger drag when zoomed)
//   View.reset()                        back to "whole map fits"
//   View.screenToWorld(sx, sy) -> {x, y}     View.worldToScreen(wx, wy) -> {x, y}
//   View.tileAt(sx, sy) -> {col, row} or null (null off the board: taps on the outer garden)
//   View.isZoomed() -> bool             true when zoomed in (so a 1-finger drag should pan)
//   View.boardRect() -> {x, y, w, h}    board on screen in CSS px (for DOM overlays/toasts)
//   View.safeRect() -> {x, y, w, h}     the current safe rect (clamped to the canvas)
//   View.sync(game)                     refit if the map changed (Renderer.draw calls this)
// Fields: cssW, cssH, dpr, scale (css px per world px), fit (scale at zoom 1), zoom (1..4), ox, oy.
var View = (function () {
    'use strict';
    var MAX_ZOOM = 4;

    var V = {
        cssW: 0, cssH: 0, dpr: 1,
        W: 960, H: 640,          // world size of the current map (game.W / game.H)
        fit: 1, zoom: 1, scale: 1,
        ox: 0, oy: 0,
        _mapKey: null,
        _safe: null,             // {x, y, w, h} as given, or null = the full canvas
    };

    function clamp(v, lo, hi) { return v < lo ? lo : v > hi ? hi : v; }

    function mapKey(game) { return game ? game.W + 'x' + game.H + '#' + game.mapIndex : null; }

    // The safe rect clamped to the canvas (the full canvas if it is missing or degenerate).
    function safe() {
        var s = V._safe;
        if (!s) return { x: 0, y: 0, w: V.cssW, h: V.cssH };
        var x0 = clamp(s.x, 0, V.cssW), y0 = clamp(s.y, 0, V.cssH);
        var x1 = clamp(s.x + s.w, 0, V.cssW), y1 = clamp(s.y + s.h, 0, V.cssH);
        if (x1 - x0 < 2 || y1 - y0 < 2) return { x: 0, y: 0, w: V.cssW, h: V.cssH };
        return { x: x0, y: y0, w: x1 - x0, h: y1 - y0 };
    }

    // Keep the board in the safe rect: centered when it fits, otherwise its edges stop at the
    // safe rect's edges (so no part of the board can stay hidden under the header/panel).
    function clampOffsets() {
        var s = safe(), bw = V.W * V.scale, bh = V.H * V.scale;
        V.ox = bw <= s.w ? s.x + (s.w - bw) / 2 : clamp(V.ox, s.x + s.w - bw, s.x);
        V.oy = bh <= s.h ? s.y + (s.h - bh) / 2 : clamp(V.oy, s.y + s.h - bh, s.y);
    }

    function refit() {
        var s = safe();
        V.fit = s.w > 0 && s.h > 0 ? Math.min(s.w / V.W, s.h / V.H) : 1;
        V.scale = V.fit * V.zoom;
    }

    // Refit, keeping the world point at the (old) safe rect's center there when zoomed in.
    function refitKeepingCenter(before) {
        var center = before && V.zoom > 1 ? V.screenToWorld(before.x + before.w / 2, before.y + before.h / 2) : null;
        refit();
        if (center) {
            var s = safe();
            V.ox = s.x + s.w / 2 - center.x * V.scale;
            V.oy = s.y + s.h / 2 - center.y * V.scale;
        }
        clampOffsets();
    }

    V.resize = function (cssW, cssH, dpr, game) {
        if (!(cssW >= 2 && cssH >= 2)) return;   // before layout: keep the last good size
        var before = V.cssW > 1 && V.cssH > 1 ? safe() : null;
        V.cssW = Math.max(1, cssW || 1);
        V.cssH = Math.max(1, cssH || 1);
        // Cap at 2 (matches main.js): sharp enough on phones, ~55% less fill than 3x.
        V.dpr = clamp(dpr || 1, 1, 2);
        var key = mapKey(game);
        if (game) { V.W = game.W; V.H = game.H; }
        if (key !== V._mapKey) { V._mapKey = key; V.zoom = 1; before = null; }
        refitKeepingCenter(before);
    };

    V.setSafeRect = function (x, y, w, h) {
        var next = (x === undefined || x === null) ? null : { x: +x || 0, y: +y || 0, w: +w || 0, h: +h || 0 };
        var cur = V._safe;
        if (!next && !cur) return;
        if (next && cur && next.x === cur.x && next.y === cur.y && next.w === cur.w && next.h === cur.h) return;
        var before = V.cssW > 1 ? safe() : null;
        V._safe = next;
        if (V.cssW > 1) refitKeepingCenter(before);
    };

    V.safeRect = function () { return safe(); };

    // Called every frame by Renderer.draw: a new map (different size) refits the camera.
    V.sync = function (game) {
        if (!game) return;
        var key = mapKey(game);
        if (key !== V._mapKey || game.W !== V.W || game.H !== V.H) {
            V._mapKey = key;
            V.W = game.W; V.H = game.H;
            V.zoom = 1;
            refit();
            clampOffsets();
        }
    };

    V.zoomAt = function (sx, sy, factor) {
        if (!(factor > 0)) return;
        var w = V.screenToWorld(sx, sy);
        V.zoom = clamp(V.zoom * factor, 1, MAX_ZOOM);
        V.scale = V.fit * V.zoom;
        V.ox = sx - w.x * V.scale;
        V.oy = sy - w.y * V.scale;
        clampOffsets();
    };

    V.panBy = function (dx, dy) {
        V.ox += dx || 0;
        V.oy += dy || 0;
        clampOffsets();
    };

    V.reset = function () {
        V.zoom = 1;
        refit();
        clampOffsets();
    };

    V.isZoomed = function () { return V.zoom > 1.001; };

    V.screenToWorld = function (sx, sy) {
        return { x: (sx - V.ox) / V.scale, y: (sy - V.oy) / V.scale };
    };

    V.worldToScreen = function (wx, wy) {
        return { x: wx * V.scale + V.ox, y: wy * V.scale + V.oy };
    };

    V.tileAt = function (sx, sy) {
        var w = V.screenToWorld(sx, sy);
        var TILE = 64;
        var col = Math.floor(w.x / TILE), row = Math.floor(w.y / TILE);
        var cols = Math.round(V.W / TILE), rows = Math.round(V.H / TILE);
        if (col < 0 || row < 0 || col >= cols || row >= rows) return null;
        return { col: col, row: row };
    };

    V.boardRect = function () {
        return { x: V.ox, y: V.oy, w: V.W * V.scale, h: V.H * V.scale };
    };

    return V;
})();

if (typeof module !== 'undefined') module.exports = View;
