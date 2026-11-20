// Garden Guard++ web: focus fire and multi-select (port of focusfire.cpp; owner: Hank).
// Double-click (double-tap) a plant: select every plant of that type. With plants selected,
// click a bug: they all shoot it whenever it's in range until it dies, escapes or splits.
// Esc (or the ✕ Cancel focus button) cancels a focus first, then deselects.
'use strict';

const FocusFire = (() => {
  const COIN = '💰';
  const $ = (id) => document.getElementById(id);
  const v = () => UI.view;
  const g = () => UI.game;

  function selection() {
    const view = v();
    if (view.group.length > 1) return view.group.slice();
    if (view.selectedId >= 0) return [view.selectedId];
    return [];
  }

  function onDoubleClick(col, row) {
    const view = v(), game = g();
    if (UI.overlayOpen() || game.isOver() || view.placing >= 0 || view.aimingId >= 0) return;
    const t = game.towerAt(col, row);
    if (!t) return;
    view.selectedId = t.id;
    view.group = game.towers.filter((o) => o.type === t.type).map((o) => o.id);
    const def = DATA.TOWERS[t.type];
    const coarse = matchMedia('(hover: none) and (pointer: coarse)').matches;
    UI.toast(`${def.icon} ${def.name} × ${view.group.length} selected: ${coarse ? 'tap' : 'click'} a bug to focus fire`);
  }

  // Touch needs a slightly bigger target than a mouse.
  function tryClick() {
    if (v().selectedId < 0) return false;
    const r = DATA.TILE * (matchMedia('(pointer: coarse)').matches ? 0.9 : 0.6);
    const bug = g().enemyNear(UI.st.lastClick.x, UI.st.lastClick.y, r);
    if (bug < 0) return false;
    focusOn(bug);
    return true;
  }

  function focusOn(enemyId) {
    const game = g();
    const ids = selection();
    const target = game.enemies.find((e) => e.id === enemyId);
    if (!target || !ids.length) return;
    let anyCanHit = false;
    for (const id of ids) { const t = game.getTower(id); if (t && game.canHit(t, target)) anyCanHit = true; }
    const bug = DATA.ENEMIES[target.type];
    if (!anyCanHit) {
      const first = game.getTower(ids[0]);
      UI.fail(`${first ? DATA.TOWERS[first.type].name : 'That plant'} can't hit flyers. Ground it with a 🍇 Grape Vine first!`);
      return;
    }
    if (!game.focusFire(ids, enemyId)) { UI.fail("Support plants can't focus fire"); return; }
    UI.sfx('click');
    UI.toast(`🎯 Focus fire on the ${bug.icon} ${bug.name}! (Esc cancels)`);
  }

  function cancelOnSelection() {
    const game = g();
    let cancelled = false;
    for (const id of selection()) {
      const t = game.getTower(id);
      if (t && t.focusId >= 0) { game.clearFocus(id); cancelled = true; }
    }
    if (cancelled) UI.toast('Focus fire cancelled');
    return cancelled;
  }

  function upgradeAllInGroup() {
    const game = g(), view = v();
    if (view.group.length <= 1) return false;
    let done = 0;
    for (;;) {   // cheapest upgrades first, as many as the coins allow
      let pick = null, best = 0;
      for (const id of view.group) {
        const o = game.getTower(id);
        const c = o ? game.upgradeCost(o) : -1;
        if (c >= 0 && c <= game.coins && (!pick || c < best)) { pick = o; best = c; }
      }
      if (!pick || !game.upgradeTower(pick.id)) break;
      done++;
    }
    if (done === 0) UI.fail('Not enough coins to upgrade any of them');
    else { UI.sfx('click'); UI.toast(`Upgraded ${done} plant${done === 1 ? '' : 's'}`); }
    return true;
  }

  const setText = (el, s) => {
    if (matchMedia('(hover: none) and (pointer: coarse)').matches) s = s.replace(' (Esc cancels)', '');
    if (el.textContent !== s) el.textContent = s;
  };

  // Called from UI.sync every frame while a plant is selected.
  function syncPanel(t, over) {
    const game = g(), view = v();
    view.group = view.group.filter((id) => game.getTower(id));
    let focusing = 0;
    if (view.group.length > 1) {
      const def = DATA.TOWERS[t.type];
      let total = 0;
      for (const id of view.group) {
        const o = game.getTower(id);
        const c = game.upgradeCost(o);
        if (c > 0) total += c;
        if (o.focusId >= 0) focusing++;
      }
      const coarse = matchMedia('(hover: none) and (pointer: coarse)').matches;
      setText($('tpName'), `${def.name} × ${view.group.length}`);
      setText($('tpLevel'), focusing ? `🎯 ${focusing} focusing (Esc cancels)` : `${coarse ? 'Tap' : 'Click'} a bug to focus fire`);
      setText($('tpUpgrade'), total > 0 ? `Upgrade all · ${COIN} ${total}` : 'All at max level');
      $('tpUpgrade').disabled = over || total <= 0;
      $('tpSell').classList.add('hidden');
      $('tpAim').classList.add('hidden');
    } else {
      $('tpSell').classList.remove('hidden');
      if (t.focusId >= 0) {
        focusing = 1;
        const e = game.enemies.find((x) => x.id === t.focusId);
        if (e) setText($('tpLevel'), `🎯 Focusing the ${DATA.ENEMIES[e.type].icon} ${DATA.ENEMIES[e.type].name} (Esc cancels)`);
      }
    }
    // Touch has no Esc key: a button cancels the focus.
    $('tpCancelFocus').classList.toggle('hidden', focusing === 0);
  }

  return { selection, onDoubleClick, tryClick, focusOn, cancelOnSelection, upgradeAllInGroup, syncPanel };
})();
