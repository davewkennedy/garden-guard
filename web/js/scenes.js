// Garden Guard++ web: staged screenshot scenes (port of MainWindow::setupScene; owner: Hank).
'use strict';

const Scenes = (() => {
  function plant(game, id, col, row, upgrades) {
    const i = DATA.TOWERS.findIndex((t) => t.id === id);
    if (i < 0) return -1;
    const t = game.placeTower(i, col, row);
    if (!t) return -1;
    for (let u = 0; u < upgrades; u++) game.upgradeTower(t.id);
    return t.id;
  }
  function advanceTo(game, target) {
    while (game.mapIndex < target) { game.phase = Game.Phase.MapCleared; game.nextMap(); }
  }

  function setup(scene, targetMap) {
    const game = UI.game, view = UI.view;
    UI.closeCard('ovIntro');
    if (scene === 'mapselect') { UI.showMapSelect(game.totalMaps - 1); return; }
    if (scene === 'journal') {
      game.quiet = true; advanceTo(game, targetMap); game.quiet = false;
      UI.toggleJournal();
      return;
    }
    const known = ['play', 'placing', 'mapclear', 'win', 'lose', 'night', 'celebrate', 'bugs', 'aim', 'newbugs', 'focus', 'vine'];
    if (!known.includes(scene)) { console.warn('unknown scene', scene); return; }
    if (scene === 'vine' && targetMap === 0) targetMap = 1;
    if (scene === 'newbugs' && targetMap === 0) targetMap = 1;
    if (scene === 'night') DATA.MAPS.forEach((m, i) => { if (m.night) targetMap = i; });

    game.quiet = true;   // staging makes no sounds or toasts
    advanceTo(game, targetMap);
    UI.st.mapStartLives = game.lives;
    game.coins = 5000;
    const selected = plant(game, 'sprout', 1, 2, 2);
    const frost = plant(game, 'frost', 3, 3, 1);
    plant(game, 'mortar', 4, 4, 0);
    let cactus = plant(game, 'cactus', 8, 4, 2);
    if (cactus < 0) cactus = frost;
    if (scene === 'aim') game.setFirePoint(selected, 2 * 64 + 32, 4 * 64 + 32);
    plant(game, 'mushroom', 3, 5, 1);
    plant(game, 'chili', 5, 5, 2);
    plant(game, 'sunflower', 0, 8, 1);
    let grape = -1;
    const lanes = game.map().skyLanes || [];
    if (scene === 'vine' && lanes.length) {
      const lane = lanes[0];
      const a = lane[Math.min(1, lane.length - 1)], b = lane[Math.min(2, lane.length - 1)];
      const lx = (a[0] + b[0]) / 2, ly = (a[1] + b[1]) / 2;
      const tiles = [];
      for (let r = 0; r < game.rows; r++) for (let c = 0; c < game.cols; c++) tiles.push([Math.hypot(c - lx, r - ly), c, r]);
      tiles.sort((p, q) => p[0] - q[0]);
      for (const t of tiles) if ((grape = plant(game, 'grape', t[1], t[2], 2)) >= 0) break;
    }
    game.coins = 1234;
    game.wave = scene === 'bugs' ? game.totalWaves - 1 : Math.min(3, game.totalWaves - 1);
    game.startWave();
    for (let i = 0; i < 60 * (scene === 'bugs' ? 24 : 9); i++) game.update(1 / 60);
    if (scene === 'vine') {
      const lassoed = () => game.enemies.some((e) => e.alive && game.grounded(e) && e.vineBy >= 0);
      game.lives = 999;
      for (let i = 0; i < 3600 && !lassoed(); i++) {
        if (game.phase === Game.Phase.Build) game.startWave();
        else if (game.phase !== Game.Phase.Wave) break;
        game.update(1 / 60);
      }
      for (let i = 0; i < 20 && lassoed(); i++) game.update(1 / 60);
      view.selectedId = grape;
      game.lives = DATA.START_LIVES;
    }
    game.quiet = false;

    if (scene === 'celebrate') {
      setTimeout(() => UI.celebrate(), 300);
    } else if (scene === 'focus') {
      const count = {};
      for (const o of game.towers) {
        const k = DATA.TOWERS[o.type].projectile;
        if (k !== DATA.Projectile.None && k !== DATA.Projectile.Aura) count[o.type] = (count[o.type] || 0) + 1;
      }
      let type = -1, best = 0;
      for (const ty in count) if (count[ty] > best) { best = count[ty]; type = Number(ty); }
      view.group = game.towers.filter((o) => o.type === type).map((o) => o.id);
      if (view.group.length) view.selectedId = view.group[view.group.length - 1];
      const sel = game.getTower(view.selectedId);
      let lead = null;
      for (const e of game.enemies) if (sel && (!lead || Math.hypot(e.x - sel.x, e.y - sel.y) < Math.hypot(lead.x - sel.x, lead.y - sel.y))) lead = e;
      UI.setUserPaused(true);
      if (lead) game.focusFire(view.group, lead.id);
    } else if (scene === 'aim') {
      view.selectedId = cactus;
      view.aimingId = cactus;
      view.hover = { col: 10, row: 5, valid: true };
    } else if (scene === 'play' || scene === 'night' || scene === 'bugs') {
      view.selectedId = selected;
      UI.toast(`Wave ${game.wave - 1} cleared! +40 💰  ·  Sunflowers +28 💰`);
    } else if (scene === 'placing') {
      view.placing = game.isUnlocked(4) ? 4 : 1;
      UI.onHover(7, 4);
    } else if (scene === 'mapclear') {
      UI.showMapCleared();
    } else if (scene === 'newbugs') {
      UI.showNewBugs();
    } else if (scene === 'win' || scene === 'lose') {
      UI.showEnd(scene === 'win');
    }
  }

  return { setup };
})();
