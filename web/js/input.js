// Garden Guard++ web: mouse, touch and keyboard input for the board (owner: Hank).
// Port of boardwidget.cpp's mouse handling, plus touch: tap = click, double-tap = double-click,
// long-press = hover (shows the placement/range preview), 1-finger drag when zoomed = pan,
// 2-finger pinch = zoom. Mouse wheel zooms on PC.
'use strict';

const Input = (() => {
  const TAP_MOVE = 10;          // px a finger may wander and still count as a tap
  const DOUBLE_MS = 330;        // second tap within this time on the same tile = double-tap
  const LONG_MS = 450;
  const MIN_TILE_PX = 34, TARGET_TILE_PX = 46;   // touch: auto-zoom when tiles are smaller than this
  let canvas = null;
  const pointers = new Map();   // pointerId -> {x, y, startX, startY, t, moved, type}
  let pinch = null;             // {dist, mx, my}
  let lastTap = null;           // {t, col, row}
  let longTimer = 0, longFired = false;

  function local(e) {
    const r = canvas.getBoundingClientRect();
    return { x: e.clientX - r.left, y: e.clientY - r.top };
  }
  function tileAt(x, y) {
    if (View.tileAt) return View.tileAt(x, y);
    const w = View.screenToWorld(x, y);
    const g = UI.game;
    const col = Math.floor(w.x / DATA.TILE), row = Math.floor(w.y / DATA.TILE);
    return col >= 0 && row >= 0 && col < g.cols && row < g.rows ? { col, row } : null;
  }
  function zoomed() { return View.isZoomed ? View.isZoomed() : false; }

  // iOS Safari only lets audio start inside touchend/click (not pointerdown), and suspends it in the
  // background, so try on every gesture; Sfx.unlock() returns at once when audio is running.
  function unlockAudio() {
    try { if (typeof Sfx !== 'undefined' && Sfx.unlock) Sfx.unlock(); } catch (e) { /* */ }
  }

  function hoverAt(x, y) {
    const t = tileAt(x, y);
    if (t) UI.onHover(t.col, t.row); else UI.onHoverLeft();
  }

  function tap(x, y, touch) {
    const t = tileAt(x, y);
    if (!t) return;
    // Big maps on phones: tiles can be ~20px. A placing/aiming tap there zooms in first,
    // so a fat-finger tap never spends coins on the wrong tile.
    const tilePx = DATA.TILE * (View.scale || 1);
    if (touch && tilePx < MIN_TILE_PX && (UI.view.placing >= 0 || UI.view.aimingId >= 0)) {
      View.zoomAt(x, y, TARGET_TILE_PX / tilePx);
      UI.onHover(t.col, t.row);
      UI.toast(UI.view.placing >= 0 ? '🔍 Zoomed in: tap again to plant' : '🔍 Zoomed in: tap again to aim');
      lastTap = null;
      return;
    }
    const w = View.screenToWorld(x, y);
    const now = performance.now();
    if (lastTap && now - lastTap.t < DOUBLE_MS && lastTap.col === t.col && lastTap.row === t.row) {
      lastTap = null;
      UI.onDoubleClick(t.col, t.row);   // like Qt: the second press is only a double-click
      return;
    }
    lastTap = { t: now, col: t.col, row: t.row };
    UI.onHover(t.col, t.row);
    UI.onClick(t.col, t.row, w.x, w.y);
  }

  function onDown(e) {
    unlockAudio();
    canvas.setPointerCapture && canvas.setPointerCapture(e.pointerId);
    const p = local(e);
    pointers.set(e.pointerId, { x: p.x, y: p.y, startX: p.x, startY: p.y, t: performance.now(), moved: false, type: e.pointerType });
    if (e.pointerType === 'mouse') {
      if (e.button !== 0) return;
      return;   // mouse clicks fire on pointerup (so a drag can pan instead)
    }
    if (pointers.size === 2) {
      clearTimeout(longTimer);
      const [a, b] = [...pointers.values()];
      a.moved = b.moved = true;   // a pinch is never a tap
      pinch = { dist: Math.hypot(a.x - b.x, a.y - b.y), mx: (a.x + b.x) / 2, my: (a.y + b.y) / 2 };
      return;
    }
    // Placing or aiming: show the preview under the finger right away.
    if (UI.view.placing >= 0 || UI.view.aimingId >= 0) hoverAt(p.x, p.y);
    longFired = false;
    clearTimeout(longTimer);
    longTimer = setTimeout(() => {
      const q = pointers.get(e.pointerId);
      if (!q || q.moved) return;
      longFired = true;
      hoverAt(q.x, q.y);
      info(q.x, q.y);
    }, LONG_MS);
  }

  // Long-press info: what's on that tile.
  function info(x, y) {
    const t = tileAt(x, y);
    if (!t) return;
    const g = UI.game;
    const w = View.screenToWorld(x, y);
    const bugId = g.enemyNear(w.x, w.y, DATA.TILE * 0.9);
    if (bugId >= 0) {
      const e = g.enemies.find((b) => b.id === bugId);
      if (e) {
        const d = DATA.ENEMIES[e.type];
        UI.toast(`${d.icon} ${d.name}: ${Math.ceil(e.hp)}/${Math.ceil(e.maxHp)} HP${d.flying ? ' · flying' : ''}${d.desc ? ' · ' + d.desc : ''}`);
        return;
      }
    }
    const tower = g.towerAt(t.col, t.row);
    if (tower) {
      const def = DATA.TOWERS[tower.type];
      UI.toast(`${def.icon} ${def.name} (level ${tower.level + 1}): ${def.desc}`);
    }
  }

  function onMove(e) {
    const p = local(e);
    const q = pointers.get(e.pointerId);
    if (!q) {   // mouse hover without a button pressed
      if (e.pointerType === 'mouse') hoverAt(p.x, p.y);
      return;
    }
    const dx = p.x - q.x, dy = p.y - q.y;
    q.x = p.x; q.y = p.y;
    if (Math.hypot(q.x - q.startX, q.y - q.startY) > TAP_MOVE) { if (!q.moved) clearTimeout(longTimer); q.moved = true; }

    if (pinch && pointers.size >= 2) {
      const [a, b] = [...pointers.values()];
      const dist = Math.hypot(a.x - b.x, a.y - b.y);
      const mx = (a.x + b.x) / 2, my = (a.y + b.y) / 2;
      if (pinch.dist > 0 && dist > 0) View.zoomAt(mx, my, dist / pinch.dist);
      View.panBy(mx - pinch.mx, my - pinch.my);
      pinch = { dist, mx, my };
      return;
    }
    // Zoomed in: any drag pans (taps still plant/select).
    if (q.moved && zoomed()) {
      q.panning = true;
      View.panBy(dx, dy);
      return;
    }
    // A finger dragging while placing moves the preview (lift to plant).
    if (e.pointerType !== 'mouse' && (UI.view.placing >= 0 || UI.view.aimingId >= 0)) hoverAt(p.x, p.y);
    if (e.pointerType === 'mouse') hoverAt(p.x, p.y);
  }

  function onUp(e) {
    const q = pointers.get(e.pointerId);
    pointers.delete(e.pointerId);
    clearTimeout(longTimer);
    if (pointers.size < 2) pinch = null;
    if (!q || e.type === 'pointercancel') return;
    if (q.type === 'mouse' && e.button !== 0) return;
    const p = local(e);
    if (q.panning) return;
    // A tap, or (touch while placing/aiming) a drag that ends on a tile: plant/aim there.
    const placingDrag = q.type !== 'mouse' && q.moved && !zoomed() && (UI.view.placing >= 0 || UI.view.aimingId >= 0);
    if (q.moved && !placingDrag) return;
    if (longFired) { longFired = false; return; }   // long-press only shows info
    tap(p.x, p.y, q.type !== 'mouse');
    if (q.type !== 'mouse' && UI.view.placing < 0 && UI.view.aimingId < 0) {
      // No hover on touch: clear the preview after a tap.
      setTimeout(() => { if (UI.view.placing < 0 && UI.view.aimingId < 0) UI.onHoverLeft(); }, 0);
    }
  }

  function onWheel(e) {
    e.preventDefault();
    const p = local(e);
    View.zoomAt(p.x, p.y, Math.exp(-e.deltaY * 0.0015));
  }

  function onKey(e) {
    if (e.target && (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA')) return;
    unlockAudio();
    // Enter/Space on a focused card button: let the browser click it.
    if ((e.key === 'Enter' || e.key === ' ') && UI.overlayOpen()) return;
    if (UI.onKey(e)) e.preventDefault();
  }

  function init(c) {
    canvas = c;
    canvas.addEventListener('pointerdown', onDown);
    canvas.addEventListener('pointermove', onMove);
    canvas.addEventListener('pointerup', onUp);
    canvas.addEventListener('pointercancel', onUp);
    canvas.addEventListener('pointerleave', (e) => { if (e.pointerType === 'mouse') UI.onHoverLeft(); });
    canvas.addEventListener('wheel', onWheel, { passive: false });
    canvas.addEventListener('contextmenu', (e) => e.preventDefault());
    window.addEventListener('keydown', onKey);
    // Any first touch/click anywhere unlocks sound (browsers require a gesture).
    window.addEventListener('pointerdown', unlockAudio, { capture: true });
    window.addEventListener('touchend', unlockAudio, { capture: true });
    window.addEventListener('click', unlockAudio, { capture: true });
    // iOS: stop the page itself from pinch-zooming or bouncing.
    document.addEventListener('gesturestart', (e) => e.preventDefault());
    document.addEventListener('touchmove', (e) => { if (e.target === canvas) e.preventDefault(); }, { passive: false });
  }

  return { init };
})();
