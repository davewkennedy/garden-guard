// Garden Guard++ web: the 📖 Garden Journal. Port of src/journal.cpp (owner: Boomhauer).
// An encyclopedia: every bug met so far (bugs in this map's and earlier maps' waves, plus what they split
// into) and every plant unlocked so far. Things not met yet are teased as "???" rows.
// API:
//   Journal.render(el, game, onClose?)  fills `el` with the journal (title, intro, two columns, and a
//                                       "Close (J)" button if onClose is given). Call it each time the
//                                       journal opens: it reads game.mapIndex and game.isUnlocked(i).
//   Journal.metBugs(game) -> bool[]     which ENEMIES entries count as met (used by render).
// The overlay, pausing and the J / 📖 toggle belong to ui.js (Hank). Plays Sfx 'click' on Close.
var Journal = (function () {
  'use strict';

  var CSS =
    '.ggj{display:flex;flex-direction:column;gap:8px;height:100%;min-height:0;box-sizing:border-box;' +
    'font:13px/1.35 system-ui,-apple-system,"Segoe UI",sans-serif;color:#222;}' +
    '.ggj-title{font-size:20px;font-weight:700;margin:0;}' +
    '.ggj-intro{margin:0;color:#444;}' +
    '.ggj-cols{flex:1 1 auto;min-height:0;overflow-y:auto;-webkit-overflow-scrolling:touch;display:grid;' +
    'grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:4px 24px;align-content:start;padding-right:6px;}' +
    '.ggj-col{display:flex;flex-direction:column;gap:6px;}' +
    '.ggj-h{font-size:17px;font-weight:700;color:#2e7d32;padding-bottom:4px;}' +
    '.ggj-row{display:flex;gap:10px;padding:2px 0;}' +
    '.ggj-ic{flex:0 0 34px;text-align:center;font-size:24px;line-height:1.1;}' +
    '.ggj-ic.unk{color:#9a9a9a;}' +
    '.ggj-name{font-weight:700;font-size:14px;}' +
    '.ggj-unk .ggj-name{color:#8a8a8a;}.ggj-unk .ggj-detail{color:#9a9a9a;}' +
    '.ggj-stats{color:#6b6b6b;}.ggj-tip{color:#2e7d32;}' +
    '.ggj-close{align-self:center;min-height:40px;padding:6px 22px;border-radius:20px;border:1px solid #3d8b40;' +
    'background:#4caf50;color:#fff;font-weight:600;font-size:15px;cursor:pointer;}' +
    '@media (max-height:430px){.ggj-title{font-size:17px}.ggj-intro{font-size:12px}.ggj-h{font-size:15px}}';

  function injectCss() {
    if (document.getElementById('ggj-css')) return;
    var s = document.createElement('style');
    s.id = 'ggj-css';
    s.textContent = CSS;
    document.head.appendChild(s);
  }

  function esc(s) {
    return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
  }

  function speedWord(s) {
    return s < 0.6 ? 'very slow' : s < 1.0 ? 'slow' : s < 1.4 ? 'medium' : s < 2.0 ? 'fast' : 'very fast';
  }

  // One entry: a big icon, then a bold name and smaller detail lines (detail is trusted HTML).
  function entry(icon, name, detailHtml, known) {
    if (!known) {
      return '<div class="ggj-row ggj-unk"><div class="ggj-ic unk">❔</div><div>' +
        '<div class="ggj-name">???</div><div class="ggj-detail">Unlocks on a later map.</div></div></div>';
    }
    return '<div class="ggj-row"><div class="ggj-ic">' + esc(icon) + '</div><div>' +
      '<div class="ggj-name">' + esc(name) + '</div><div class="ggj-detail">' + detailHtml + '</div></div></div>';
  }

  function metBugs(game) {
    var met = DATA.ENEMIES.map(function () { return false; });
    var upTo = Math.min(game.mapIndex, DATA.MAPS.length - 1);
    for (var m = 0; m <= upTo; m++) {
      var ws = DATA.MAPS[m].waves.length ? DATA.MAPS[m].waves : DATA.WAVES;
      ws.forEach(function (w) { w.groups.forEach(function (g) { met[g.enemy] = true; }); });
    }
    DATA.ENEMIES.forEach(function (d, k) { if (met[k] && d.splitInto >= 0) met[d.splitInto] = true; });
    return met;
  }

  function unlocked(game, i) {
    return typeof game.isUnlocked === 'function' ? !!game.isUnlocked(i) : true;
  }

  function render(el, game, onClose) {
    injectCss();
    var met = metBugs(game);
    var nMet = met.filter(Boolean).length;

    var bugs = '<div class="ggj-col"><div class="ggj-h">🐞 Bugs met (' + nMet + ' of ' + DATA.ENEMIES.length + ')</div>';
    for (var pass = 0; pass < 2; pass++) {
      DATA.ENEMIES.forEach(function (d, k) {
        if (met[k] !== (pass === 0)) return;
        var tags = [];
        if (d.boss) tags.push("<b style='color:#c62828'>boss</b>");
        if (d.wild) tags.push("<b style='color:#7b3fa0'>wildcard flyer</b>");
        else if (d.flying) tags.push("<b style='color:#1565c0'>flying</b>");
        if (d.armor > 0) tags.push('armor ' + d.armor);
        var stats = 'HP ' + d.hp + ' · ' + speedWord(d.speed);
        if (tags.length) stats += ' · ' + tags.join(' · ');
        stats += ' · ' + d.reward + ' 💰';
        var detail = esc(d.desc) + "<br><span class='ggj-stats'>" + stats + '</span>';
        bugs += entry(d.icon, d.golden ? '✨ ' + d.name : d.name, detail, met[k]);
      });
    }
    bugs += '</div>';

    var nUnlocked = 0;
    DATA.TOWERS.forEach(function (t, i) { if (unlocked(game, i)) nUnlocked++; });
    var plants = '<div class="ggj-col"><div class="ggj-h">🌱 Plants unlocked (' + nUnlocked + ' of ' + DATA.TOWERS.length + ')</div>';
    for (var p = 0; p < 2; p++) {
      DATA.TOWERS.forEach(function (d, i) {
        var known = unlocked(game, i);
        if (known !== (p === 0)) return;
        var detail = esc(d.desc);
        if (d.counters) detail += "<br><span class='ggj-tip'>Tip: " + esc(d.counters) + '</span>';
        detail += "<br><span class='ggj-stats'>Cost " + d.levels[0].cost + ' 💰';
        if (d.levels[0].range > 0) detail += ' · range ' + d.levels[0].range + ' tiles';
        detail += '</span>';
        plants += entry(d.icon, d.name, detail, known);
      });
    }
    plants += '</div>';

    el.innerHTML = '<div class="ggj">' +
      '<h2 class="ggj-title">📖 Garden Journal</h2>' +
      '<p class="ggj-intro">Every bug you\'ve met and every plant you\'ve grown. New pages fill in as you travel.</p>' +
      '<div class="ggj-cols">' + bugs + plants + '</div>' +
      (onClose ? '<button type="button" class="ggj-close">Close (J)</button>' : '') +
      '</div>';
    if (onClose) {
      var btn = el.querySelector('.ggj-close');
      btn.addEventListener('click', function () {
        if (typeof Sfx !== 'undefined') Sfx.play('click');
        onClose();
      });
      try { btn.focus({ preventScroll: true }); } catch (e) {}
    }
  }

  return { render: render, metBugs: metBugs, speedWord: speedWord };
})();
