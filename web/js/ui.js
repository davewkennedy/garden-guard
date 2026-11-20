// Garden Guard++ web: the HUD, shop, plant panel, cards and the game-event wiring.
// Port of mainwindow.cpp (owner: Hank). Focus fire / multi-select is in focusfire.js.
'use strict';

const UI = (() => {
  const $ = (id) => document.getElementById(id);
  const COIN = '💰';
  const BUMP_MS = 300, TOAST_MS = 1800;
  const P = () => DATA.Projectile;
  const Phase = () => Game.Phase;

  // ---------- saves (localStorage, "ggweb:" prefix) ----------
  function load(key, def) {
    try { const v = localStorage.getItem('ggweb:' + key); return v === null ? def : JSON.parse(v); }
    catch (e) { return def; }
  }
  function save(key, value) {
    try { localStorage.setItem('ggweb:' + key, JSON.stringify(value)); } catch (e) { /* private mode */ }
  }

  // What the renderer shows besides the game (the C++ View struct).
  const view = { hover: null, placing: -1, selectedId: -1, aimingId: -1, group: [], realTime: 0 };
  const st = {
    userPaused: false, speed: 1, auto: false, lastThink: -1, buildStart: -1,
    cardSince: 0, cardShown: null, lastClick: { x: 0, y: 0 },
    runKills: 0, runPlanted: 0, runShots: 0, runCoins: 0,
    mapStartLives: 20, runStars: [], primed: false, panelKey: '',
    shopMap: -1, shopUnlockAll: false, shopOrder: [], shopItems: [],
  };
  let game = null, autopilot = null;
  let toastTimer = 0;

  // ---------- small helpers ----------
  function sfx(name) { try { if (typeof Sfx !== 'undefined') Sfx.play(name); } catch (e) { /* no audio */ } }
  function setText(el, s) { if (el.textContent !== s) el.textContent = s; }
  function setHtml(el, s) { if (el._html !== s) { el._html = s; el.innerHTML = s; } }
  function show(el, on) { el.classList.toggle('hidden', !on); }
  function esc(s) { return String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c])); }
  function starsText(n) { return '★'.repeat(n) + '☆'.repeat(3 - n); }
  function livesText(l) { return `${l} ${l === 1 ? 'life' : 'lives'}`; }
  function starsFor(lost) { return lost <= 0 ? 3 : lost <= 3 ? 2 : 1; }
  function wavesOf(m) { const md = DATA.MAPS[m]; return md.waves && md.waves.length ? md.waves : DATA.WAVES; }
  function keyFor(i) { return i < 9 ? String(i + 1) : i === 9 ? '0' : i === 10 ? '-' : ''; }
  function attacks(def) { return def.projectile !== P().None && def.projectile !== P().Aura; }
  function groundOnly(def) { return def.projectile === P().Mortar || def.projectile === P().Flame; }

  // Touch screens: no keyboard, so drop key hints like "(Esc cancels)" and "(A to stop)".
  const TOUCH = () => matchMedia('(hover: none) and (pointer: coarse)').matches;
  function untouch(s) { return TOUCH() ? s.replace(/ \((Esc cancels|A to stop|Esc|J|T)\)/g, '') : s; }
  function toast(text, error = false) {
    text = untouch(text);
    const t = $('toast');
    t.textContent = text;
    t.classList.toggle('error', !!error);
    show(t, true);
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => show(t, false), TOAST_MS);
  }
  function fail(msg) { sfx('error'); toast(msg, true); }

  const OVERLAYS = ['ovIntro', 'ovMapSelect', 'ovNext', 'ovBugs', 'ovEnd', 'ovJournal'];
  function isOpen(id) { return !$(id).classList.contains('hidden'); }
  function overlayOpen() { return OVERLAYS.some(isOpen); }
  function openCard(id) { show($(id), true); const b = $(id).querySelector('button.primary'); if (b) setTimeout(() => b.focus(), 0); }
  function closeCard(id) { show($(id), false); }
  function paused() { return st.userPaused || overlayOpen() || document.hidden; }

  // ---------- header ----------
  function setStat(el, text) {
    if (el.textContent === text) return;
    el.textContent = text;
    if (!st.primed) return;
    el.classList.add('bump');
    clearTimeout(el._bump);
    el._bump = setTimeout(() => el.classList.remove('bump'), BUMP_MS);
  }
  function setUserPaused(on) {
    st.userPaused = on;
    const b = $('btnPause');
    b.textContent = on ? '▶' : '⏸';
    b.title = on ? 'Resume (Space)' : 'Pause (Space)';
    b.classList.toggle('on', on);
    show($('pausedBanner'), on);
  }
  function setSpeed(s) {
    st.speed = Math.max(1, Math.min(3, s));
    $('btnSpeed').textContent = st.speed + '×';
    $('btnSpeed').classList.toggle('on', st.speed > 1);
  }
  function soundOn() { return typeof Sfx === 'undefined' ? false : !Sfx.muted; }
  function syncSoundButton() {
    const on = soundOn();
    $('btnSound').textContent = on ? '🔊' : '🔇';
    $('btnSound').title = on ? 'Sound on' : 'Sound off';
  }
  function setAuto(on) {
    st.auto = on;
    $('btnAuto').classList.toggle('on', on);
    st.lastThink = st.buildStart = -1;
    st.cardShown = null;
    if (on) {
      setSpeed(2);
      setUserPaused(false);
      toast('🤖 Autopilot on: sit back and watch (A to stop)');
    } else {
      toast('🤖 Autopilot off: your turn!');
    }
  }

  // ---------- full screen ----------
  const svg = (d) => `<svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="#24331c" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round"><path d="${d}"/></svg>`;
  const FS_ENTER = svg('M4 9V4h5M15 4h5v5M20 15v5h-5M9 20H4v-5');
  const FS_EXIT = svg('M9 4v5H4M20 9h-5V4M15 20v-5h5M4 15h5v5');
  function isStandalone() {
    return window.navigator.standalone === true ||
      (window.matchMedia && matchMedia('(display-mode: standalone), (display-mode: fullscreen)').matches);
  }
  function canFullscreen() {
    const d = document.documentElement;
    return !!(d.requestFullscreen || d.webkitRequestFullscreen);
  }
  function fullscreenNow() { return !!(document.fullscreenElement || document.webkitFullscreenElement); }
  function toggleFullscreen() {
    sfx('click');
    if (fullscreenNow()) {
      (document.exitFullscreen || document.webkitExitFullscreen).call(document);
      return;
    }
    const d = document.documentElement;
    const req = d.requestFullscreen || d.webkitRequestFullscreen;
    // iPhone Safari has no full screen for pages: "Add to Home Screen" instead.
    if (!req || /iPhone|iPod/.test(navigator.userAgent)) {
      toast('📱 For full screen on iPhone: tap Share, then "Add to Home Screen"');
      return;
    }
    try {
      const p = req.call(d, { navigationUI: 'hide' });
      const lock = () => { try { const o = screen.orientation; if (o && o.lock) o.lock('landscape').catch(() => {}); } catch (e) { /* not supported */ } };
      if (p && p.then) p.then(lock).catch(() => toast('Full screen is not available here', true));
      else lock();
    } catch (e) { toast('Full screen is not available here', true); }
  }
  function syncFullButton() {
    const b = $('btnFull');
    show(b, !isStandalone());
    const fs = fullscreenNow();
    if (b._fs !== fs) {
      b._fs = fs;
      // Inline SVG: the ⛶ character is missing from many phone fonts.
      b.innerHTML = fs ? FS_EXIT : FS_ENTER;
      b.title = fs ? 'Leave full screen' : 'Full screen';
    }
  }

  // ---------- shop ----------
  function makeShopItem(type, index) {
    const def = DATA.TOWERS[type];
    const key = keyFor(index);
    const b = document.createElement('button');
    b.className = 'shopItem';
    const fresh = game.mapIndex > 0 && def.unlockMap === game.mapIndex && !game.unlockAll;
    b.title = `${def.name}${key ? ' (key ' + key + ')' : ''}\n${def.desc}` +
      (!attacks(def) ? '' : groundOnly(def) ? '\n✗ Ground only: can\'t hit flyers' : '\n✓ Hits flyers too') +
      (fresh ? `\n✨ New plant! ${def.counters || ''}` : '');
    b.innerHTML = `<span class="shopIcon">${def.icon}</span><span class="shopCost">${COIN} ${def.levels[0].cost}</span>` +
      (key ? `<span class="shopKey">${key}</span>` : '<span></span>') +
      `<span class="shopName">${esc(def.name)}</span>` + (fresh ? '<span class="shopNew">NEW</span>' : '');
    if (fresh) b.classList.add('fresh');
    b.addEventListener('click', () => toggleShop(type));
    return b;
  }
  // Only the plants unlocked on this map: older unlocks first, so keys stay the same between maps.
  function rebuildShop() {
    st.shopMap = game.mapIndex;
    st.shopUnlockAll = game.unlockAll;
    const shop = $('shop');
    shop.innerHTML = '';
    st.shopOrder = [];
    for (let i = 0; i < DATA.TOWERS.length; i++) if (game.isUnlocked(i)) st.shopOrder.push(i);
    st.shopOrder.sort((a, b) => DATA.TOWERS[a].unlockMap - DATA.TOWERS[b].unlockMap);   // stable in modern JS
    st.shopItems = st.shopOrder.map((type, i) => { const b = makeShopItem(type, i); shop.appendChild(b); return b; });
  }
  function toggleShop(type) {
    if (overlayOpen() || game.isOver() || !game.isUnlocked(type)) return;
    if (view.placing === type) { view.placing = -1; sfx('click'); return; }
    if (game.coins < DATA.TOWERS[type].levels[0].cost) { fail('Not enough coins'); return; }
    view.placing = type;
    view.selectedId = -1;
    sfx('click');
  }
  function shopInfoHtml(type) {
    const def = DATA.TOWERS[type];
    const fresh = game.mapIndex > 0 && def.unlockMap === game.mapIndex;
    return `<b>${def.icon} ${esc(def.name)}</b>: ${esc(def.desc)}` +
      (!attacks(def) ? '' : groundOnly(def) ? '<br><i>✗ Ground only: can\'t hit flyers</i>' : '<br><i>✓ Hits flyers too</i>') +
      (fresh && def.counters ? `<br>✨ ${esc(def.counters)}` : '');
  }

  // ---------- plant panel ----------
  function fillPanel(t) {
    const key = [t.id, t.type, t.level, t.hasFirePoint, view.group.length, t.focusId >= 0].join(':');
    if (key === st.panelKey) return;
    st.panelKey = key;
    const def = DATA.TOWERS[t.type];
    const lv = def.levels[t.level];
    setText($('tpIcon'), def.icon);
    setText($('tpName'), def.name);
    setText($('tpLevel'), `Level ${t.level + 1} / ${def.levels.length}`);
    const rows = [];
    const row = (n, v) => rows.push(`<tr><td>${n}</td><td>${v}</td></tr>`);
    if (lv.income > 0) {
      row('Income', `+${lv.income} ${COIN} per wave`);
    } else if (def.projectile === P().Aura) {
      row('Range', lv.range + ' tiles');
      row('Boost', `+${Math.round(lv.boost * 100)}% fire rate to nearby plants`);
    } else {
      row('Damage', t.hasFirePoint ? `${game.damageOf(t)} (🎯 +${Math.round((Game.AMBUSH_BONUS - 1) * 100)}%)` : String(lv.damage));
      row('Air', groundOnly(def) ? '✗ ground only' : '✓ hits flyers');
      row('Range', lv.range + ' tiles');
      row('Fire rate', lv.rate + '/s');
      if (lv.slow > 0) row('Slow', `${Math.round(lv.slow * 100)}% for ${lv.slowTime}s`);
      if (lv.splash > 0) row('Splash', lv.splash + ' tiles');
      if (lv.pierce > 0) row('Pierce', `${lv.pierce} bugs`);
      if (lv.shots > 0) row('Volley', `${lv.shots} targets`);
      if (lv.poisonDps > 0) row('Poison', `${lv.poisonDps}/s for ${lv.poisonTime}s`);
      if (lv.groundTime > 0) row('Grounds', `flyers for ${lv.groundTime}s`);
      if (def.projectile === P().Flame) row('Spread', `${Math.round(DATA.FLAME_HALF_ANGLE * 2 * 180 / Math.PI)}° cone`);
    }
    $('tpStats').innerHTML = rows.join('');
  }

  // ---------- per-frame sync ----------
  function isValid(col, row) {
    if (!game.canPlace(col, row)) return false;
    return view.placing < 0 || game.coins >= DATA.TOWERS[view.placing].levels[0].cost;
  }

  function sync() {
    const over = game.isOver();
    if (over) { view.placing = -1; view.selectedId = -1; }

    setStat($('statMap'), `🗺️ ${game.map().name} (${game.mapIndex + 1}/${game.totalMaps})`);
    setStat($('statCoins'), `${COIN} ${game.coins}`);
    setStat($('statLives'), `❤️ ${game.lives}`);
    setStat($('statWave'), `🌊 ${game.wave} / ${game.totalWaves}`);
    $('statLives').classList.toggle('low', game.lives <= 5);

    if (st.shopMap !== game.mapIndex || st.shopUnlockAll !== game.unlockAll) rebuildShop();
    if (view.placing >= 0 && !game.isUnlocked(view.placing)) view.placing = -1;
    st.shopItems.forEach((b, i) => {
      const type = st.shopOrder[i];
      const disabled = over || game.coins < DATA.TOWERS[type].levels[0].cost;
      if (b.disabled !== disabled) b.disabled = disabled;
      b.classList.toggle('checked', view.placing === type);
    });
    const info = $('shopInfo');
    show(info, view.placing >= 0);
    if (view.placing >= 0) setHtml(info, shopInfoHtml(view.placing));

    $('board').classList.toggle('cross', view.placing >= 0 || view.aimingId >= 0);
    if (view.aimingId >= 0 && view.aimingId !== view.selectedId) view.aimingId = -1;
    if (view.hover) view.hover.valid = isValid(view.hover.col, view.hover.row);

    // Plant panel
    let t = view.selectedId >= 0 ? game.getTower(view.selectedId) : null;
    if (view.selectedId >= 0 && !t) view.selectedId = -1;
    show($('towerPanel'), !!t);
    if (t) {
      fillPanel(t);
      const cost = game.upgradeCost(t);
      setText($('tpUpgrade'), cost < 0 ? 'Max level' : `Upgrade · ${COIN} ${cost}`);
      $('tpUpgrade').disabled = over || cost < 0 || game.coins < cost;
      setText($('tpSell'), `Sell · ${COIN} ${game.sellValue(t)}`);
      $('tpSell').disabled = over;
      const canAim = attacks(DATA.TOWERS[t.type]);
      show($('tpAim'), canAim);
      setText($('tpAim'), t.hasFirePoint ? '🎯 Clear fire point'
        : view.aimingId === t.id ? (TOUCH() ? '🎯 Tap a spot… ✕' : '🎯 Pick a spot… (Esc)')
        : TOUCH() ? '🎯 Set fire point' : '🎯 Set fire point (T)');
      $('tpAim').disabled = over;
      FocusFire.syncPanel(t, over);
    } else {
      st.panelKey = '';
      view.group = [];
    }

    // Start wave button + next wave preview
    const next = Math.min(game.wave + 1, game.totalWaves);
    setText($('startWave'), game.phase === Phase().Wave ? `Wave ${game.wave}…` : `Start wave ${next}`);
    $('startWave').disabled = !(game.phase === Phase().Build && game.wave < game.totalWaves);
    let preview = '';
    if (game.phase === Phase().Build && game.wave < game.totalWaves) {
      const counts = new Array(DATA.ENEMIES.length).fill(0);
      for (const g of game.waves()[game.wave].groups) counts[g.enemy] += g.count;
      const parts = [];
      let boss = false, air = false;
      counts.forEach((n, i) => {
        if (!n) return;
        parts.push(`${DATA.ENEMIES[i].icon}×${n}`);
        boss = boss || DATA.ENEMIES[i].boss;
        air = air || DATA.ENEMIES[i].flying;
      });
      preview = 'Next: ' + parts.join('  ');
      if (air) preview += '\n✈️ Flyers incoming: mortars and chilis can\'t hit them';
      if (boss) preview += '\n⚠️ Boss wave!';
    }
    setText($('nextWave'), preview);
    show($('nextWave'), !!preview);

    show($('btnZoomReset'), typeof View !== 'undefined' && View.isZoomed && View.isZoomed());
    syncFullButton();
    st.primed = true;
  }

  // ---------- board input (called by input.js with tile coords + world point) ----------
  function onHover(col, row) { view.hover = { col, row, valid: isValid(col, row) }; }
  function onHoverLeft() { view.hover = null; }

  function onClick(col, row, wx, wy) {
    st.lastClick = { x: wx, y: wy };
    if (overlayOpen() || game.isOver()) return;
    if (view.aimingId >= 0) {
      const x = col * DATA.TILE + DATA.TILE / 2, y = row * DATA.TILE + DATA.TILE / 2;
      if (!game.setFirePoint(view.aimingId, x, y)) { fail('Out of range: pick a spot inside the circle'); return; }
      sfx('place');
      toast('🎯 Ambush set! This plant now waits for bugs at that spot.');
      view.aimingId = -1;
      return;
    }
    if (view.placing >= 0) {
      const cost = DATA.TOWERS[view.placing].levels[0].cost;
      if (!game.canPlace(col, row)) { fail("Can't plant there"); return; }
      if (game.coins < cost) { fail('Not enough coins'); return; }
      if (!game.placeTower(view.placing, col, row)) { fail("Can't plant there"); return; }
      if (game.coins < cost) view.placing = -1;   // keep planting while affordable
      onHover(col, row);
      return;
    }
    if (FocusFire.tryClick()) return;   // a bug was clicked with plants selected
    const t = game.towerAt(col, row);
    view.selectedId = t ? t.id : -1;
    view.group = [];
  }

  function onDoubleClick(col, row) { FocusFire.onDoubleClick(col, row); }

  // Esc / ✕: cancel aiming, then placing, then focus fire, then the selection.
  function cancel() {
    if (view.aimingId >= 0) view.aimingId = -1;
    else if (view.placing >= 0) view.placing = -1;
    else if (!FocusFire.cancelOnSelection()) { view.selectedId = -1; view.group = []; }
  }

  function aimClick() {
    const t = game.getTower(view.selectedId);
    if (!t || game.isOver()) return;
    sfx('click');
    if (t.hasFirePoint) {
      game.clearFirePoint(t.id);
      toast('🎯 Fire point cleared: back to automatic targeting');
    } else if (view.aimingId === t.id) {
      view.aimingId = -1;
    } else {
      view.aimingId = t.id;
      toast(matchMedia('(pointer: coarse)').matches ? '🎯 Tap a spot inside the range circle'
        : '🎯 Click a spot inside the range circle (Esc cancels)');
    }
  }

  function onKey(e) {
    if (e.ctrlKey || e.altKey || e.metaKey) return false;
    const k = e.key;
    if ((k === 'j' || k === 'J') && !e.repeat && (isOpen('ovJournal') || !overlayOpen())) { toggleJournal(); return true; }
    if (overlayOpen()) return false;
    switch (k) {
      case 'Escape': cancel(); return true;
      case ' ': if (!e.repeat) { sfx('click'); setUserPaused(!st.userPaused); } return true;
      case 'f': case 'F': if (!e.repeat) { sfx('click'); setSpeed(st.speed % 3 + 1); } return true;
      case 'n': case 'N': if (!e.repeat && game.startWave()) sfx('click'); return true;
      case 't': case 'T':
        if (!e.repeat && view.selectedId >= 0 && !$('tpAim').classList.contains('hidden') && !$('tpAim').disabled) aimClick();
        return true;
      case 'a': case 'A': if (!e.repeat) { sfx('click'); setAuto(!st.auto); } return true;
    }
    const n = k === '0' ? 9 : k === '-' ? 10 : (k >= '1' && k <= '9') ? Number(k) - 1 : -1;
    if (!e.repeat && n >= 0) { if (n < st.shopOrder.length) toggleShop(st.shopOrder[n]); return true; }
    return false;
  }

  // ---------- cards ----------
  function clearView() { view.placing = -1; view.selectedId = -1; view.aimingId = -1; view.hover = null; view.group = []; }

  function totalStars() { let s = 0; for (let i = 0; i < DATA.MAPS.length; i++) s += load('stars/' + i, 0); return s; }
  function furthestMap() { return Math.min(Math.floor(load('best', 0) / 100), DATA.MAPS.length - 1); }

  function buildIntro() {
    let text = `Bugs are marching on your house! Plant defenders beside the path to stop them, and upgrade ` +
      `them with the coins you earn. Survive ${wavesOf(0).length} waves in each of ${DATA.MAPS.length} gardens. ` +
      `New bugs (even flying ones!) appear as you go, and your lives carry over between gardens.`;
    const best = load('best', 0);
    if (best > 0) {
      const m = Math.min(Math.floor(best / 100), DATA.MAPS.length - 1);
      text += `\n\n🏆 Your best: ${DATA.MAPS[m].name}, wave ${best % 100}`;
    }
    const stars = totalStars();
    if (stars > 0) text += `\n⭐ ${stars} of ${3 * DATA.MAPS.length} stars earned`;
    $('introText').textContent = text;
    const furthest = furthestMap();
    show($('btnContinue'), furthest > 0);
    show($('btnChoose'), furthest > 0);
    if (furthest > 0) $('btnContinue').textContent = `Continue from ${DATA.MAPS[furthest].name} (map ${furthest + 1})`;
  }

  const SEASON_ICON = { Spring: '🌸', Summer: '☀️', Autumn: '🍂', Winter: '❄️' };
  function showMapSelect(furthest) {
    furthest = Math.max(0, Math.min(furthest, DATA.MAPS.length - 1));
    const list = $('mapList');
    list.innerHTML = '';
    for (let i = 0; i <= furthest; i++) {
      const m = DATA.MAPS[i];
      const stars = Math.max(0, Math.min(3, load('stars/' + i, 0)));
      const name = document.createElement('div');
      name.className = 'mName';
      name.textContent = `${i + 1}. ${m.name} ${SEASON_ICON[m.season] || ''}`;
      const s = document.createElement('div');
      s.className = 'mStars' + (stars > 0 ? ' got' : '');
      s.textContent = starsText(stars);
      const play = document.createElement('button');
      play.textContent = 'Play';
      play.addEventListener('click', () => startRunAt(i));
      list.append(name, s, play);
    }
    closeCard('ovIntro');
    openCard('ovMapSelect');
  }

  // Bugs that appear on this map but on no earlier map, the plants it unlocks, and its rules.
  function showNewBugs() {
    const m = game.mapIndex;
    if (m === 0) return;
    const typesOn = (i) => { const seen = new Array(DATA.ENEMIES.length).fill(false); for (const w of wavesOf(i)) for (const g of w.groups) seen[g.enemy] = true; return seen; };
    const before = new Array(DATA.ENEMIES.length).fill(false);
    for (let i = 0; i < m; i++) typesOn(i).forEach((v, k) => { before[k] = before[k] || v; });
    const lines = [];
    typesOn(m).forEach((v, k) => {
      if (!v || before[k]) return;
      const d = DATA.ENEMIES[k];
      let line = `${d.icon}  <b>${esc(d.name)}</b>`;
      if (d.flying) line += ' <i>(flying)</i>';
      if (d.desc) line += '<br>' + esc(d.desc);
      lines.push(line);
    });
    const plants = [];
    for (const def of DATA.TOWERS) {
      if (def.unlockMap !== m) continue;
      plants.push(`${def.icon}  <b>${esc(def.name)}</b>` + (def.counters ? ': ' + esc(def.counters) : ''));
    }
    const md = DATA.MAPS[m];
    const rules = [];
    let wild = false;
    for (const w of wavesOf(m)) for (const g of w.groups) wild = wild || DATA.ENEMIES[g.enemy].wild;
    let water = false, rocks = false;
    for (const t of md.terrain || []) {
      water = water || t[2] === DATA.TERRAIN_WATER || t[2] === DATA.TERRAIN_LILY;
      rocks = rocks || t[2] === DATA.TERRAIN_ROCK;
    }
    if (md.night) rules.push('🌙 <b>Night</b>: the garden is dark, so watch closely');
    if (wild) rules.push('🦇 <b>Wild bats</b> ignore the sky lanes and swoop straight for your house');
    if (md.windEvery > 0) rules.push('🌬️ <b>Wind gusts</b> blow flyers off course; a 🍇 Grape Vine keeps them grounded');
    if (md.bugSpeedMult < 1) rules.push('❄️ <b>Deep snow</b> slows ground bugs, but frost is weaker');
    if (water) rules.push('🪷 <b>Ponds</b>: plant on the lily pads, not the water');
    if (rocks) rules.push('🪨 <b>Boulders</b> block planting');
    if (md.path2 && md.path2.length) rules.push('🔀 <b>Two entrances</b>: bugs come from two directions');
    if (!lines.length && !plants.length && !rules.length) return;
    const bugsWord = lines.length === 1 ? 'New bug' : 'New bugs';
    let title;
    if (!lines.length && !plants.length) title = `${md.name}: watch out!`;
    else if (!plants.length) title = bugsWord + '!';
    else if (!lines.length) title = plants.length === 1 ? 'New plant!' : 'New plants!';
    else title = `${bugsWord}… and new ${plants.length === 1 ? 'plant' : 'plants'} to stop them!`;
    let html = lines.join('<br><br>');
    if (plants.length) html += `${lines.length ? '<br><br>' : ''}<span style="color:#2f8a3a;font-weight:800">🌱 Unlocked in your shop:</span><br>${plants.join('<br>')}`;
    if (rules.length) html += `${html ? '<br><br>' : ''}<span style="color:#a0683c;font-weight:800">🗺️ On this map:</span><br>${rules.join('<br>')}`;
    $('bugsTitle').textContent = title;
    $('bugsText').innerHTML = html;
    openCard('ovBugs');
  }

  function awardStars() {
    const stars = starsFor(st.mapStartLives - game.lives);
    st.runStars.push(stars);
    const key = 'stars/' + game.mapIndex;
    if (stars > load(key, 0)) save(key, stars);
  }

  function showMapCleared() {
    clearView();
    if (game.isLastMap()) { showEnd(true); return; }
    const next = DATA.MAPS[game.mapIndex + 1];
    const lost = st.mapStartLives - game.lives;
    const stars = starsFor(lost);
    $('nextTitle').textContent = `${game.map().name} cleared!\n${starsText(stars)}`;
    $('nextText').textContent =
      `${stars === 3 ? 'Flawless: not a single bug got through!' : `${livesText(lost)} lost here (lose none for ★★★).`}\n` +
      `You kept ${livesText(game.lives)}. Next up: ${next.name}. ${next.blurb}\n\n` +
      'Your coins and plants reset for the new garden, but your lives carry over.';
    $('btnNext').textContent = `On to ${next.name}`;
    openCard('ovNext');
  }

  function difficultyLabel() {
    try { if (typeof Difficulty !== 'undefined' && Difficulty.label) return Difficulty.label(); } catch (e) { /* */ }
    return 'Normal';
  }
  function applyDifficulty() {
    try { if (typeof Difficulty !== 'undefined' && Difficulty.apply) Difficulty.apply(game); } catch (e) { console.error(e); }
  }

  function showEnd(won) {
    clearView();
    $('endTitle').textContent = won ? 'You saved every garden!' : 'The bugs got through!';
    let text = won ? `You cleared all ${game.totalMaps} maps with ${livesText(game.lives)} left.`
      : `You reached wave ${game.wave} of ${game.totalWaves} on ${game.map().name} (map ${game.mapIndex + 1} of ${game.totalMaps}).`;
    if (st.runStars.length) {
      let total = 0;
      const lines = st.runStars.map((s, i) => { total += s; return `${DATA.MAPS[i + st.runStartMap].name} ${starsText(s)}`; });
      text += `\n\n${lines.join('\n')}\n${total} of ${3 * game.totalMaps} stars`;
    }
    text += `\n\n🐜 ${st.runKills} bugs squashed   🌱 ${st.runPlanted} plants planted\n🎯 ${st.runShots} shots fired   ${COIN} ${st.runCoins} coins earned`;
    text += `\nDifficulty: ${difficultyLabel()}`;
    $('endText').textContent = text;
    openCard('ovEnd');
  }

  function resetRunStats(startMap) {
    st.runKills = st.runPlanted = st.runShots = st.runCoins = 0;
    st.runStars = [];
    st.runStartMap = startMap;
  }

  function goToNextMap() {
    sfx('click');
    game.nextMap();
    clearView();
    setUserPaused(false);
    if (!st.auto) setSpeed(1);
    closeCard('ovNext');
    st.primed = false;
    showNewBugs();
  }

  function startRunAt(map) {
    sfx('click');
    resetRunStats(map);
    applyDifficulty();
    game.reset(map);
    st.mapStartLives = game.lives;
    clearView();
    setUserPaused(false);
    if (!st.auto) setSpeed(1);
    st.primed = false;
    closeCard('ovIntro');
    closeCard('ovMapSelect');
    showNewBugs();
  }

  function playAgain() {
    resetRunStats(0);
    applyDifficulty();
    game.reset();
    st.mapStartLives = game.lives;
    clearView();
    setUserPaused(false);
    setSpeed(1);
    closeCard('ovEnd');
    st.primed = false;
    sfx('click');
    buildIntro();
  }

  function toggleJournal() {
    sfx('click');
    if (isOpen('ovJournal')) { closeCard('ovJournal'); return; }
    try {
      if (typeof Journal !== 'undefined' && Journal.render) Journal.render($('journalHost'), game);
      else $('journalHost').textContent = 'The journal is still being written…';
    } catch (e) { console.error(e); $('journalHost').textContent = 'The journal could not be opened.'; }
    openCard('ovJournal');
  }

  // ---------- autopilot (as MainWindow::runAutopilot) ----------
  function runAutopilot() {
    if (!st.auto || !autopilot) return;
    if (isOpen('ovIntro')) closeCard('ovIntro');
    if (isOpen('ovEnd')) { setAuto(false); return; }
    for (const id of ['ovNext', 'ovBugs']) {
      if (!isOpen(id)) continue;
      const now = performance.now();
      if (st.cardShown !== id) { st.cardShown = id; st.cardSince = now; }
      if (now - st.cardSince > (id === 'ovBugs' ? 4000 : 2500)) {
        st.cardShown = null;
        if (id === 'ovNext') goToNextMap(); else closeCard('ovBugs');
      }
      return;
    }
    if (game.isOver() || paused()) return;
    if (game.time < st.lastThink || game.time - st.lastThink >= 0.5) {
      st.lastThink = game.time;
      if (autopilot.think) autopilot.think(); else if (autopilot.step) autopilot.step();
    }
    if (game.phase === Phase().Build) {
      if (st.buildStart < 0 || game.time < st.buildStart) st.buildStart = game.time;
      if (game.time - st.buildStart >= 2.0) { game.startWave(); st.buildStart = -1; }
    } else {
      st.buildStart = -1;
    }
  }

  // ---------- game events -> sounds and messages ----------
  function wireGame() {
    const on = (n, f) => { if (game.on) game.on(n, f); };
    on('shot', (type) => { st.runShots++; sfx('shoot_' + DATA.TOWERS[type].id); });
    on('killed', (reward) => { st.runKills++; st.runCoins += reward; sfx('kill'); });
    on('placed', () => { st.runPlanted++; sfx('place'); });
    on('bugSound', (name) => sfx(name));
    on('leaked', () => sfx('leak'));
    on('upgraded', () => sfx('upgrade'));
    on('sold', () => sfx('sell'));
    on('gust', () => toast('🌬️ A gust of wind! Flyers are blown off course'));
    on('wildcard', () => toast('🦇 A wild bat appears! Catch it for 30 coins!'));
    on('waveStarted', (wave) => {
      sfx('waveStart');
      for (const g of game.waves()[wave - 1].groups) {
        const d = DATA.ENEMIES[g.enemy];
        if (d.boss) { toast(`⚠️ Boss incoming: ${d.icon} ${d.name}!`, true); break; }
      }
      const progress = game.mapIndex * 100 + wave;
      if (progress > load('best', 0)) save('best', progress);
    });
    on('waveEnded', (wave, bonus, harvest) => {
      st.runCoins += bonus + harvest;
      sfx('waveEnd');
      let msg = `Wave ${wave} cleared! +${bonus} ${COIN}`;
      if (harvest > 0) { sfx('harvest'); msg += `  ·  Sunflowers +${harvest} ${COIN}`; }
      toast(msg);
    });
    on('mapCleared', () => {
      awardStars();
      sfx('win');
      celebrate();
      setTimeout(() => { if (game.phase === Phase().MapCleared) showMapCleared(); }, 1500);
    });
    on('mapStarted', (index) => {
      st.mapStartLives = game.lives;
      toast(`Map ${index + 1} of ${game.totalMaps}: ${DATA.MAPS[index].name}`);    });
    on('won', () => {
      awardStars();
      sfx('win');
      celebrate();
      setTimeout(celebrate, 900);
      setTimeout(() => { if (game.phase === Phase().Won) showEnd(true); }, 1800);
    });
    on('lost', () => { sfx('lose'); showEnd(false); });
  }
  function celebrate() { try { if (Renderer.celebrate) Renderer.celebrate(); } catch (e) { /* */ } }

  // ---------- buttons ----------
  function wireButtons() {
    const click = (id, f) => $(id).addEventListener('click', f);
    click('btnJournal', toggleJournal);
    click('btnAuto', () => { sfx('click'); setAuto(!st.auto); });
    click('btnSpeed', () => { sfx('click'); setSpeed(st.speed % 3 + 1); });
    click('btnPause', () => { sfx('click'); setUserPaused(!st.userPaused); });
    click('btnSound', () => {
      if (typeof Sfx !== 'undefined') Sfx.muted = !Sfx.muted;   // Sfx saves it (ggweb:sound)
      syncSoundButton();
      sfx('click');
    });
    click('btnFull', toggleFullscreen);
    click('btnZoomReset', () => { sfx('click'); if (View.reset) View.reset(); });
    click('sideToggle', () => {
      const side = $('side');
      side.classList.toggle('collapsed');
      $('sideToggle').textContent = side.classList.contains('collapsed') ? '◂' : '▸';
      window.dispatchEvent(new Event('resize'));
    });
    click('tpClose', () => { sfx('click'); view.selectedId = -1; view.group = []; });
    click('tpAim', aimClick);
    click('tpCancelFocus', () => { FocusFire.cancelOnSelection(); });
    click('tpUpgrade', () => {
      const t = game.getTower(view.selectedId);
      if (!t || game.isOver()) return;
      if (FocusFire.upgradeAllInGroup()) return;
      const cost = game.upgradeCost(t);
      if (cost < 0) { fail('Already at max level'); return; }
      if (game.coins < cost) { fail('Not enough coins'); return; }
      sfx('click');
      if (!game.upgradeTower(t.id)) fail('Not enough coins');
    });
    click('tpSell', () => {
      const t = game.getTower(view.selectedId);
      if (!t || game.isOver()) return;
      sfx('click');
      game.sellTower(t.id);
      view.selectedId = -1;
    });
    click('startWave', () => { if (game.startWave()) sfx('click'); else fail("Can't start a wave now"); });
    click('btnStart', () => { sfx('click'); applyDifficulty(); game.reset(0); st.mapStartLives = game.lives; resetRunStats(0); closeCard('ovIntro'); });
    click('btnContinue', () => startRunAt(furthestMap()));
    click('btnChoose', () => { sfx('click'); showMapSelect(furthestMap()); });
    click('btnMapBack', () => { sfx('click'); closeCard('ovMapSelect'); openCard('ovIntro'); });
    click('btnNext', goToNextMap);
    click('btnBugs', () => { sfx('click'); closeCard('ovBugs'); });
    click('btnAgain', playAgain);
    click('btnJournalClose', toggleJournal);
    // Android Chrome (when hosted over https): offer "install", which then opens full screen.
    let installEvent = null;
    window.addEventListener('beforeinstallprompt', (e) => {
      e.preventDefault();
      installEvent = e;
      show($('btnInstall'), true);
    });
    click('btnInstall', () => {
      sfx('click');
      if (!installEvent) return;
      installEvent.prompt();
      installEvent = null;
      show($('btnInstall'), false);
    });
    window.addEventListener('appinstalled', () => show($('btnInstall'), false));
    document.addEventListener('fullscreenchange', syncFullButton);
    document.addEventListener('webkitfullscreenchange', syncFullButton);
  }

  function init(g, ap) {
    game = g;
    autopilot = ap;
    resetRunStats(0);
    st.mapStartLives = game.lives;
    wireGame();
    wireButtons();
    try {
      if (typeof Difficulty !== 'undefined' && Difficulty.render) Difficulty.render($('difficultyHost'), () => applyDifficulty());
    } catch (e) { console.error(e); }
    applyDifficulty();
    buildIntro();
    openCard('ovIntro');
    setUserPaused(false);
    setSpeed(1);
    syncSoundButton();
    sync();
  }

  return {
    init, sync, runAutopilot, paused, overlayOpen, onHover, onHoverLeft, onClick, onDoubleClick, onKey, cancel,
    toast, fail, sfx, view, st, setAuto, setSpeed, setUserPaused, showMapCleared, showNewBugs, showEnd,
    showMapSelect, toggleJournal, celebrate, load, save, openCard, closeCard,
    get game() { return game; },
  };
})();
