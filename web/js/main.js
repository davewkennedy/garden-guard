// Garden Guard++ web: startup, the game loop, and URL options for testing (owner: Hank).
// URL params (like the Qt hidden CLI options):
//   ?autopilot=1&speed=N   the AI plays (speed 1-3; up to 20 for test runs)
//   ?map=N                 start a fresh run on map N (1-based)
//   ?scene=NAME[&map=N]    stage a screenshot scene: play placing mapclear win lose night celebrate
//                          bugs aim newbugs focus vine journal mapselect intro
//   ?unlock=1              every plant unlocked
'use strict';

(function main() {
  const params = new URLSearchParams(location.search);
  const canvas = document.getElementById('board');
  const boardWrap = document.getElementById('boardWrap');   // where the board fits (immersive)
  const ctx = canvas.getContext('2d');
  const game = new Game();
  const autopilot = typeof Autopilot !== 'undefined' ? new Autopilot(game) : null;
  window.GG = { game, autopilot };   // handy in the console and for test runners

  UI.init(game, autopilot);
  Input.init(canvas);

  // ---------- sizing ----------
  let lastCols = -1, lastRows = -1, lastW = 0, lastH = 0, lastDpr = 0, lastSafe = '';
  function resize(force) {
    const r = canvas.getBoundingClientRect();
    // Cap at 2x: on 3x phones that cuts fill cost ~55% (battery, smoothness) and looks nearly identical.
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const w = Math.max(1, Math.round(r.width)), h = Math.max(1, Math.round(r.height));
    // The canvas fills the screen; the board itself fits inside #boardWrap (in canvas CSS px).
    const b = boardWrap ? boardWrap.getBoundingClientRect() : r;
    const safe = [Math.round(b.left - r.left), Math.round(b.top - r.top), Math.round(b.width), Math.round(b.height)];
    const safeKey = safe.join(',');
    if (!force && w === lastW && h === lastH && dpr === lastDpr && safeKey === lastSafe && game.cols === lastCols && game.rows === lastRows) return;
    lastSafe = safeKey;
    if (View.setSafeRect && safe[2] >= 2 && safe[3] >= 2) View.setSafeRect(safe[0], safe[1], safe[2], safe[3]);
    const newMap = game.cols !== lastCols || game.rows !== lastRows;
    lastW = w; lastH = h; lastDpr = dpr; lastCols = game.cols; lastRows = game.rows;
    canvas.width = Math.round(w * dpr);
    canvas.height = Math.round(h * dpr);
    View.resize(w, h, dpr, game);
    if (newMap && View.reset) View.reset();
  }
  window.addEventListener('resize', () => resize(true));
  window.addEventListener('orientationchange', () => setTimeout(() => resize(true), 200));
  if (window.ResizeObserver) {
    const ro = new ResizeObserver(() => resize(true));
    ro.observe(canvas);
    if (boardWrap) ro.observe(boardWrap);   // the side panel collapsing changes the board area only
  }

  // ---------- URL options ----------
  const maxSpeed = Number(params.get('speed')) > 3 ? Math.min(20, Number(params.get('speed'))) : 3;
  if (params.get('unlock') === '1') game.unlockAll = true;
  const startMap = Math.max(0, Math.min(DATA.MAPS.length - 1, (Number(params.get('map')) || 1) - 1));
  const scene = params.get('scene');
  if (scene && scene !== 'intro') {
    try { Scenes.setup(scene, startMap); } catch (e) { console.error('scene failed', e); }
  } else if (params.has('map') && !scene) {
    UI.startRunAt(startMap);
  }
  if (params.get('autopilot') === '1') {
    UI.setAuto(true);
    const s = Number(params.get('speed')) || 2;
    UI.st.speed = Math.max(1, Math.min(maxSpeed, s));
    document.getElementById('btnSpeed').textContent = UI.st.speed + '×';
  }

  // ---------- loop ----------
  let last = performance.now();
  let errors = 0;
  const STEP = 1 / 60;
  let acc = 0;
  function frame(now) {
    requestAnimationFrame(frame);
    const dt = Math.min(0.05, Math.max(0, (now - last) / 1000));
    last = now;
    try {
      resize(false);
      // Fixed 1/60 s steps (with an accumulator), so the game plays identically on 60 Hz and
      // 120 Hz screens and matches the headless parity runs exactly.
      if (!UI.paused()) {
        acc += dt * UI.st.speed;
        let steps = 0;
        const cap = 4 * UI.st.speed;   // don't spiral on a slow device: drop time instead
        while (acc >= STEP && steps < cap) {
          game.update(STEP);
          acc -= STEP;
          steps++;
          if (UI.st.auto && steps % 4 === 0) UI.runAutopilot();   // the AI keeps up at high speed
        }
        if (steps >= cap) acc = 0;
      } else {
        acc = 0;
      }
      UI.runAutopilot();
      UI.view.realTime = now / 1000;
      UI.sync();
      Renderer.draw(ctx, game, View, UI.view);
    } catch (e) {
      if (errors++ < 5) console.error(e);   // keep running; don't flood the console
    }
  }
  resize(true);
  frame(performance.now());   // paint once right away (slow first rAF on iPad/headless)

  // Offline support once hosted (never on file://).
  if ('serviceWorker' in navigator && /^https?:$/.test(location.protocol)) {
    navigator.serviceWorker.register('sw.js').catch(() => {});
  }
})();
