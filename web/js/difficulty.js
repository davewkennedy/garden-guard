// Garden Guard++ web: difficulty picker. Port of src/difficulty.cpp (owner: Boomhauer).
// Three toggle pills on the intro card: 🌱 Easy · 🌿 Normal · 🌳 Hard (Normal by default), saved in
// localStorage "ggweb:difficulty" (0/1/2) and applied before every run starts (Let's grow, Continue,
// Choose map, Play again). Shown in the page title and on the end card.
// API:
//   Difficulty.get() -> 0|1|2            the saved choice (default 1)
//   Difficulty.set(d)                    save a choice (no game call)
//   Difficulty.label(d?) -> "🌳 Hard"    label of d, or of the saved choice
//   Difficulty.apply(game)               game.setDifficulty(get()) + document.title "Garden Guard++ · <label>"
//   Difficulty.render(el, onChange?)     fills el with the picker; a click saves it, plays 'click' and calls
//                                        onChange(d). Hank: call Difficulty.apply(game) in onChange too
//                                        (C++ re-applies to the not-yet-started run).
var Difficulty = (function () {
  'use strict';

  var KEY = 'ggweb:difficulty';
  var NAMES = ['🌱 Easy', '🌿 Normal', '🌳 Hard'];
  var TIPS = ['Relaxed: bugs -25% HP, start coins +25%',
              'The garden as designed',
              'Seasoned gardeners: bugs +30% HP, start coins -10%'];

  var CSS =
    '.ggd{display:flex;gap:6px;width:100%;box-sizing:border-box;}' +
    '.ggd button{flex:1 1 0;min-height:36px;border:1px solid #b7d8a8;border-radius:16px;padding:4px 10px;' +
    'background:#f4faef;color:#222;font:600 14px system-ui,-apple-system,"Segoe UI",sans-serif;cursor:pointer;' +
    'touch-action:manipulation;}' +
    '.ggd button[aria-pressed="true"]{background:#4caf50;color:#fff;border-color:#3d8b40;}' +
    '.ggd button:focus-visible{outline:2px solid #2e7d32;outline-offset:2px;}';

  function clamp(d) {
    d = parseInt(d, 10);
    return isNaN(d) ? 1 : Math.max(0, Math.min(2, d));
  }

  var current = 1;
  try { var s = localStorage.getItem(KEY); if (s !== null) current = clamp(s); } catch (e) {}

  function get() { return current; }
  function set(d) {
    current = clamp(d);
    try { localStorage.setItem(KEY, String(current)); } catch (e) {}
  }
  function label(d) { return NAMES[d === undefined ? current : clamp(d)]; }

  function apply(game) {
    if (game && typeof game.setDifficulty === 'function') game.setDifficulty(current);
    try { document.title = 'Garden Guard++ · ' + label(); } catch (e) {}
  }

  function injectCss() {
    if (document.getElementById('ggd-css')) return;
    var st = document.createElement('style');
    st.id = 'ggd-css';
    st.textContent = CSS;
    document.head.appendChild(st);
  }

  function render(el, onChange) {
    injectCss();
    el.innerHTML = '';
    var row = document.createElement('div');
    row.className = 'ggd';
    row.setAttribute('role', 'group');
    row.setAttribute('aria-label', 'Difficulty');
    var buttons = NAMES.map(function (name, i) {
      var b = document.createElement('button');
      b.type = 'button';
      b.textContent = name;
      b.title = TIPS[i];
      b.setAttribute('aria-pressed', i === current ? 'true' : 'false');
      b.addEventListener('click', function () {
        if (typeof Sfx !== 'undefined') Sfx.play('click');
        set(i);
        buttons.forEach(function (o, j) { o.setAttribute('aria-pressed', j === i ? 'true' : 'false'); });
        if (onChange) onChange(i);
      });
      row.appendChild(b);
      return b;
    });
    el.appendChild(row);
  }

  return { get: get, set: set, label: label, apply: apply, render: render, NAMES: NAMES, TIPS: TIPS };
})();
