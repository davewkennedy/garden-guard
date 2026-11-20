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
// API (Hank's input.js / main.js call these):
//   View.resize(cssW, cssH, dpr, game)  fit the whole map (like setupScale); keeps zoom/center on
//                                       a plain resize, refits when the map size changes
//   View.zoomAt(sx, sy, factor)         zoom around a screen point (pinch center / mouse wheel)
//   View.panBy(dx, dy)                  move the board by screen px (1-finger drag when zoomed)
//   View.reset()                        back to "whole map fits"
//   View.screenToWorld(sx, sy) -> {x, y}     View.worldToScreen(wx, wy) -> {x, y}
//   View.tileAt(sx, sy) -> {col, row} or null (null when off the board)
//   View.isZoomed() -> bool             true when zoomed in (so a 1-finger drag should pan)
//   View.boardRect() -> {x, y, w, h}    board on screen in CSS px (for DOM overlays/toasts)
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
    };

    function clamp(v, lo, hi) { return v < lo ? lo : v > hi ? hi : v; }

    function mapKey(game) { return game ? game.W + 'x' + game.H + '#' + game.mapIndex : null; }

    // Keep the board on screen: centered when it fits, otherwise no gaps at the edges.
    function clampOffsets() {
        var bw = V.W * V.scale, bh = V.H * V.scale;
        V.ox = bw <= V.cssW ? (V.cssW - bw) / 2 : clamp(V.ox, V.cssW - bw, 0);
        V.oy = bh <= V.cssH ? (V.cssH - bh) / 2 : clamp(V.oy, V.cssH - bh, 0);
    }

    function refit() {
        V.fit = V.cssW > 0 && V.cssH > 0 ? Math.min(V.cssW / V.W, V.cssH / V.H) : 1;
        V.scale = V.fit * V.zoom;
    }

    V.resize = function (cssW, cssH, dpr, game) {
        // Remember which world point is at the screen center so rotation/resizing keeps the view.
        var hadSize = V.cssW > 0 && V.cssH > 0;
        var center = hadSize ? V.screenToWorld(V.cssW / 2, V.cssH / 2) : null;
        V.cssW = Math.max(1, cssW || 1);
        V.cssH = Math.max(1, cssH || 1);
        // Cap at 3: crisp on every phone without huge backing stores.
        V.dpr = clamp(dpr || 1, 1, 3);
        var key = mapKey(game);
        if (game) { V.W = game.W; V.H = game.H; }
        if (key !== V._mapKey) { V._mapKey = key; V.zoom = 1; center = null; }
        refit();
        if (center && V.zoom > 1) {
            V.ox = V.cssW / 2 - center.x * V.scale;
            V.oy = V.cssH / 2 - center.y * V.scale;
        }
        clampOffsets();
    };

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
