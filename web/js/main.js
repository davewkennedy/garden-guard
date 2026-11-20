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
  const ctx = canvas.getContext('2d');
  const game = new Game();
  const autopilot = typeof Autopilot !== 'undefined' ? new Autopilot(game) : null;
  window.GG = { game, autopilot };   // handy in the console and for test runners

  UI.init(game, autopilot);
  Input.init(canvas);

  // ---------- sizing ----------
  let lastCols = -1, lastRows = -1, lastW = 0, lastH = 0, lastDpr = 0;
  function resize(force) {
    const r = canvas.getBoundingClientRect();
    const dpr = Math.min(window.devicePixelRatio || 1, 3);
    const w = Math.max(1, Math.round(r.width)), h = Math.max(1, Math.round(r.height));
    if (!force && w === lastW && h === lastH && dpr === lastDpr && game.cols === lastCols && game.rows === lastRows) return;
    const newMap = game.cols !== lastCols || game.rows !== lastRows;
    lastW = w; lastH = h; lastDpr = dpr; lastCols = game.cols; lastRows = game.rows;
    canvas.width = Math.round(w * dpr);
    canvas.height = Math.round(h * dpr);
    View.resize(w, h, dpr, game);
    if (newMap && View.reset) View.reset();
  }
  window.addEventListener('resize', () => resize(true));
  window.addEventListener('orientationchange', () => setTimeout(() => resize(true), 200));
  if (window.ResizeObserver) new ResizeObserver(() => resize(true)).observe(canvas);

  // ---------- URL options ----------
  const maxSpeed = Number(params.get('speed')) > 3 ? Math.min(20, Number(params.get('speed'))) : 3;
  if (params.get('unlock') === '1') game.unlockAll = true;
  const startMap = Math.max(0, Math.min(DATA.MAPS.length - 1, (Number(params.get('map')) || 1) - 1));
  const scene = params.get('scene');
  if (scene && scene !== 'intro') {
    try { Scenes.setup(scene, startMap); } catch (e) { console.error('scene failed', e); }
  } else if (params.has('map') && !scene) {
    UI.closeCard('ovIntro');
    game.reset(startMap);
    UI.st.mapStartLives = game.lives;
    UI.showNewBugs();
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
  function frame(now) {
    requestAnimationFrame(frame);
    const dt = Math.min(0.05, Math.max(0, (now - last) / 1000));
    last = now;
    try {
      resize(false);
      if (!UI.paused()) for (let i = 0; i < UI.st.speed; i++) game.update(dt);
      UI.runAutopilot();
      UI.view.realTime = now / 1000;
      UI.sync();
      Renderer.draw(ctx, game, View, UI.view);
    } catch (e) {
      if (errors++ < 5) console.error(e);   // keep running; don't flood the console
    }
  }
  resize(true);
  requestAnimationFrame(frame);

  // Offline support once hosted (never on file://).
  if ('serviceWorker' in navigator && /^https?:$/.test(location.protocol)) {
    navigator.serviceWorker.register('sw.js').catch(() => {});
  }
})();
