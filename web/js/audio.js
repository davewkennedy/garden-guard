// Garden Guard++ web: synthesized sound effects. Port of src/audio.cpp (owner: Boomhauer).
// Same recipes and the same synth as the C++ (tone/noise parts, exponential decay envelope, RBJ biquads).
// Each sound is rendered once into an AudioBuffer, at unlock time, and then played as a BufferSource.
// API: Sfx.unlock() (call on the first pointerdown/keydown), Sfx.play(name), Sfx.muted (get/set, saved),
//      Sfx.enabled() / Sfx.setEnabled(on) (C++ names), Sfx.names().
// Failure means silence, never an exception.
var Sfx = (function () {
  'use strict';

  var MASTER_GAIN = 0.12;   // spec: <= 0.15
  var THROTTLE_MS = 50;     // same sound at most once per 50 ms
  var MAX_VOICES = 12;
  var KEY = 'ggweb:sound';

  // ---- Recipes (copied from audio.cpp) ----
  var O = { Sine: 'Sine', Triangle: 'Triangle', Square: 'Square', Saw: 'Saw', Noise: 'Noise' };
  var F = { None: 'None', Lowpass: 'Lowpass', Highpass: 'Highpass', Bandpass: 'Bandpass' };

  function tone(at, osc, freq, freqEnd, dur, gain, attack, filter, filterFreq) {
    return { at: at, osc: osc, freq: freq, freqEnd: freqEnd, dur: dur, gain: gain,
      attack: attack === undefined ? 0.005 : attack, filter: filter || F.None,
      filterFreq: filterFreq === undefined ? 1000 : filterFreq, q: 1 };
  }
  function noise(at, filter, freq, dur, gain, attack, q) {
    return { at: at, osc: O.Noise, freq: 440, freqEnd: 0, dur: dur, gain: gain,
      attack: attack === undefined ? 0.003 : attack, filter: filter, filterFreq: freq,
      q: q === undefined ? 1 : q };
  }

  function recipes() {
    var r = {};
    r.shoot_sprout = [tone(0, O.Sine, 620, 300, 0.07, 0.35)];
    r.shoot_frost = [noise(0, F.Highpass, 5000, 0.18, 0.18, 0.02),
                     tone(0, O.Triangle, 1800, 2500, 0.16, 0.07, 0.02)];
    r.shoot_mortar = [tone(0, O.Sine, 150, 48, 0.26, 0.8),
                      noise(0, F.Lowpass, 500, 0.12, 0.3)];
    r.shoot_cactus = [noise(0, F.Bandpass, 3200, 0.045, 0.45, 0.003, 2),
                      tone(0, O.Square, 1300, 800, 0.035, 0.06)];
    r.shoot_mushroom = [noise(0, F.Lowpass, 1400, 0.12, 0.3, 0.01),
                        tone(0, O.Sine, 340, 210, 0.1, 0.18, 0.01)];
    r.shoot_chili = [noise(0, F.Bandpass, 900, 0.22, 0.5, 0.02, 0.8),
                     noise(0.02, F.Highpass, 3000, 0.12, 0.12, 0.01),
                     tone(0, O.Saw, 90, 60, 0.2, 0.12, 0.02, F.Lowpass, 400)];
    r.shoot_rose = [noise(0, F.Highpass, 4200, 0.09, 0.3, 0.004),
                    tone(0, O.Triangle, 900, 1500, 0.08, 0.12, 0.005)];
    r.shoot_corn = [noise(0, F.Bandpass, 2200, 0.05, 0.4, 0.002, 3),
                    tone(0, O.Sine, 500, 900, 0.05, 0.2, 0.002)];
    r.harvest = [tone(0, O.Sine, 1047, 0, 0.12, 0.18, 0.005),
                 tone(0.08, O.Sine, 1319, 0, 0.12, 0.18, 0.005),
                 tone(0.16, O.Sine, 1568, 0, 0.2, 0.18, 0.005)];
    r.kill = [tone(0, O.Triangle, 1150, 1800, 0.09, 0.28)];
    r.leak = [tone(0, O.Saw, 110, 0, 0.38, 0.25, 0.02, F.Lowpass, 700),
              tone(0, O.Saw, 116, 0, 0.38, 0.2, 0.02, F.Lowpass, 700)];
    r.place = [tone(0, O.Sine, 240, 130, 0.16, 0.6),
               noise(0, F.Lowpass, 900, 0.05, 0.2)];
    var up = [];
    [660, 880, 1100, 1320].forEach(function (n, i) { up.push(tone(i * 0.06, O.Triangle, n, 0, 0.12, 0.22)); });
    up.push(noise(0.18, F.Highpass, 7000, 0.15, 0.08));
    r.upgrade = up;
    r.sell = [tone(0, O.Triangle, 1320, 0, 0.06, 0.25),
              tone(0.07, O.Triangle, 1760, 0, 0.24, 0.25),
              noise(0.07, F.Highpass, 6000, 0.1, 0.1)];
    r.waveStart = [tone(0, O.Saw, 392, 0, 0.18, 0.25, 0.02, F.Lowpass, 1400),
                   tone(0.2, O.Saw, 523, 0, 0.32, 0.25, 0.02, F.Lowpass, 1600)];
    r.waveEnd = [tone(0, O.Triangle, 523, 0, 0.6, 0.16, 0.02),
                 tone(0.03, O.Triangle, 659, 0, 0.6, 0.16, 0.02),
                 tone(0.06, O.Triangle, 784, 0, 0.6, 0.16, 0.02)];
    r.win = [tone(0, O.Triangle, 523, 0, 0.13, 0.25, 0.01),
             tone(0.13, O.Triangle, 659, 0, 0.13, 0.25, 0.01),
             tone(0.26, O.Triangle, 784, 0, 0.13, 0.25, 0.01),
             tone(0.39, O.Triangle, 1047, 0, 0.6, 0.25, 0.01),
             tone(0.39, O.Sine, 523 / 2, 0, 0.6, 0.12, 0.03),
             tone(0.39, O.Sine, 659 / 2, 0, 0.6, 0.12, 0.03),
             tone(0.39, O.Sine, 784 / 2, 0, 0.6, 0.12, 0.03)];
    r.lose = [tone(0, O.Triangle, 392, 0, 0.22, 0.25, 0.01),
              tone(0.22, O.Triangle, 349, 0, 0.22, 0.25, 0.01),
              tone(0.44, O.Triangle, 311, 0, 0.22, 0.25, 0.01),
              tone(0.66, O.Triangle, 262, 220, 0.45, 0.25, 0.01)];
    r.click = [tone(0, O.Sine, 1500, 0, 0.025, 0.1, 0.002)];
    r.error = [tone(0, O.Square, 180, 0, 0.08, 0.2, 0.005, F.Lowpass, 800),
               tone(0.12, O.Square, 150, 0, 0.1, 0.2, 0.005, F.Lowpass, 800)];
    // ---- Bug abilities ----
    r.bug_hop = [tone(0, O.Square, 4200, 4600, 0.03, 0.05, 0.002, F.Bandpass, 4400),
                 tone(0.045, O.Square, 4300, 4700, 0.03, 0.05, 0.002, F.Bandpass, 4400),
                 noise(0, F.Lowpass, 600, 0.05, 0.08)];
    r.bug_heal = [tone(0, O.Sine, 660, 1320, 0.32, 0.1, 0.06),
                  tone(0.04, O.Sine, 990, 1980, 0.28, 0.07, 0.06),
                  noise(0, F.Highpass, 8000, 0.3, 0.03, 0.08)];
    r.bug_split = [noise(0, F.Lowpass, 700, 0.16, 0.45, 0.002, 2),
                   tone(0, O.Sine, 220, 70, 0.14, 0.35, 0.002),
                   noise(0.05, F.Bandpass, 1600, 0.06, 0.15, 0.002, 4)];
    var fan = [];
    [1047, 1319, 1568, 2093].forEach(function (n, i) {
      fan.push(tone(i * 0.07, O.Square, n, 0, 0.1, 0.09, 0.003, F.Lowpass, 5000));
      fan.push(tone(i * 0.07, O.Sine, n * 2, 0, 0.08, 0.06, 0.003));
    });
    fan.push(tone(0.3, O.Triangle, 2093, 0, 0.3, 0.2, 0.005));
    fan.push(tone(0.3, O.Triangle, 2637, 0, 0.3, 0.14, 0.005));
    fan.push(noise(0.3, F.Highpass, 7000, 0.3, 0.08));
    r.jackpot = fan;
    r.boss_rumble = [noise(0, F.Lowpass, 160, 1.0, 0.55, 0.15, 1),
                     tone(0, O.Sine, 55, 40, 1.0, 0.35, 0.12),
                     tone(0.05, O.Sine, 82, 60, 0.8, 0.18, 0.1)];
    r.shoot_grape = [noise(0, F.Highpass, 2500, 0.1, 0.22, 0.003),
                     tone(0, O.Triangle, 300, 900, 0.1, 0.16, 0.004)];
    r.bug_screech = [tone(0, O.Square, 2600, 3400, 0.07, 0.07, 0.003, F.Bandpass, 3000),
                     tone(0.1, O.Square, 2800, 3700, 0.09, 0.07, 0.003, F.Bandpass, 3200)];
    r.gust_whoosh = [noise(0, F.Bandpass, 450, 1.1, 0.32, 0.4, 0.7),
                     noise(0.25, F.Bandpass, 900, 0.8, 0.18, 0.3, 0.9)];
    r.bug_spit = [tone(0, O.Sine, 420, 900, 0.09, 0.22, 0.003),
                  noise(0, F.Bandpass, 1400, 0.08, 0.25, 0.002, 3)];
    return r;
  }

  // ---- Offline synth (same math as audio.cpp render()) ----
  function biquad(type, freq, q, rate) {
    var f = { b0: 1, b1: 0, b2: 0, a1: 0, a2: 0, x1: 0, x2: 0, y1: 0, y2: 0 };
    if (type === F.None) return f;
    var w = 2 * Math.PI * Math.min(freq, rate * 0.49) / rate;
    var cw = Math.cos(w), alpha = Math.sin(w) / (2 * q), a0 = 1 + alpha;
    if (type === F.Lowpass) { f.b0 = (1 - cw) / 2; f.b1 = 1 - cw; f.b2 = (1 - cw) / 2; }
    else if (type === F.Highpass) { f.b0 = (1 + cw) / 2; f.b1 = -(1 + cw); f.b2 = (1 + cw) / 2; }
    else { f.b0 = alpha; f.b1 = 0; f.b2 = -alpha; }
    f.b0 /= a0; f.b1 /= a0; f.b2 /= a0;
    f.a1 = -2 * cw / a0;
    f.a2 = (1 - alpha) / a0;
    return f;
  }

  var seed = 12345;
  function white() {   // deterministic LCG in [-1, 1)
    seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0;
    return seed / 2147483648 - 1;
  }

  function renderParts(parts, rate) {
    var total = 0;
    parts.forEach(function (p) {
      total = Math.max(total, Math.floor(p.at * rate) + Math.floor(p.dur * rate));
    });
    var buf = new Float32Array(Math.max(1, total));
    parts.forEach(function (p) {
      var flt = biquad(p.filter, p.filterFreq, p.filter === F.Bandpass ? p.q : 0.707, rate);
      var start = Math.floor(p.at * rate), len = Math.floor(p.dur * rate), phase = 0;
      var decayLen = Math.max(1e-6, p.dur - p.attack), ratio = 0.0001 / p.gain;
      for (var i = 0; i < len; i++) {
        var t = i / rate, env, s;
        if (t < p.attack) env = 0.0001 + (p.gain - 0.0001) * t / p.attack;
        else env = p.gain * Math.pow(ratio, (t - p.attack) / decayLen);
        if (p.osc === O.Noise) {
          s = white();
        } else {
          var fr = p.freqEnd > 0 ? p.freq * Math.pow(p.freqEnd / p.freq, t / p.dur) : p.freq;
          phase += fr / rate;
          phase -= Math.floor(phase);
          if (p.osc === O.Sine) s = Math.sin(2 * Math.PI * phase);
          else if (p.osc === O.Triangle) s = 4 * Math.abs(phase - 0.5) - 1;
          else if (p.osc === O.Square) s = phase < 0.5 ? 1 : -1;
          else s = 2 * phase - 1;
        }
        if (p.filter !== F.None) {
          var y = flt.b0 * s + flt.b1 * flt.x1 + flt.b2 * flt.x2 - flt.a1 * flt.y1 - flt.a2 * flt.y2;
          flt.x2 = flt.x1; flt.x1 = s; flt.y2 = flt.y1; flt.y1 = y;
          s = y;
        }
        buf[start + i] += s * env * MASTER_GAIN;
      }
    });
    for (var k = 0; k < buf.length; k++) buf[k] = Math.max(-1, Math.min(1, buf[k]));
    return buf;
  }

  // ---- Playback ----
  var RECIPES = recipes();
  var ctx = null, failed = false, buffers = {}, lastPlayed = {}, voices = 0;
  var enabled = true;
  try { var saved = localStorage.getItem(KEY); if (saved !== null) enabled = saved !== '0'; } catch (e) {}

  function now() { return (typeof performance !== 'undefined' ? performance.now() : Date.now()); }

  function ensureCtx() {
    if (ctx || failed) return ctx;
    try {
      var AC = window.AudioContext || window.webkitAudioContext;
      if (!AC) { failed = true; return null; }
      ctx = new AC();
    } catch (e) { failed = true; ctx = null; }
    return ctx;
  }

  function bufferFor(name) {
    if (buffers[name]) return buffers[name];
    var parts = RECIPES[name];
    if (!parts) return null;
    var data = renderParts(parts, ctx.sampleRate);
    var b = ctx.createBuffer(1, data.length, ctx.sampleRate);
    if (b.copyToChannel) b.copyToChannel(data, 0); else b.getChannelData(0).set(data);
    buffers[name] = b;
    return b;
  }

  // Safe to call on every gesture (pointerdown/touchend/click/keydown): cheap no-op while running.
  // Creates the context lazily inside the gesture; whenever it isn't 'running' (first use, or iOS
  // suspended/'interrupted' it after backgrounding) it resumes it and plays a 1-sample silent buffer.
  var prerendered = false;
  function unlock() {
    if (ctx && ctx.state === 'running') return;
    if (!ensureCtx()) return;
    try {
      if (ctx.resume) { var pr = ctx.resume(); if (pr && pr.catch) pr.catch(function () {}); }
      var b = ctx.createBuffer(1, 1, ctx.sampleRate), src = ctx.createBufferSource();
      src.buffer = b; src.connect(ctx.destination); src.start(0);
    } catch (e) {}
    if (!prerendered) {
      prerendered = true;
      // Pre-render every sound just after the gesture so the first shots don't stutter.
      setTimeout(function () {
        try { Object.keys(RECIPES).forEach(bufferFor); } catch (e) {}
      }, 0);
    }
  }

  function play(name) {
    if (!enabled || failed || !ctx) return;   // no context until the first gesture (unlock)
    try {
      if (ctx.state !== 'running') return;   // suspended/interrupted: the next gesture resumes it
      var t = now();
      if (lastPlayed[name] !== undefined && t - lastPlayed[name] < THROTTLE_MS) return;
      if (voices >= MAX_VOICES) return;
      var b = bufferFor(name);
      if (!b) return;
      lastPlayed[name] = t;
      var src = ctx.createBufferSource();
      src.buffer = b;
      src.connect(ctx.destination);
      voices++;
      src.onended = function () { voices = Math.max(0, voices - 1); };
      src.start(0);
    } catch (e) {}
  }

  function setEnabled(on) {
    enabled = !!on;
    try { localStorage.setItem(KEY, enabled ? '1' : '0'); } catch (e) {}
    if (enabled) unlock();
  }

  var api = {
    unlock: unlock,
    play: play,
    enabled: function () { return enabled; },
    setEnabled: setEnabled,
    names: function () { return Object.keys(RECIPES); },
    _render: renderParts, _recipes: RECIPES,   // for tests
  };
  Object.defineProperty(api, 'muted', {
    get: function () { return !enabled; },
    set: function (m) { setEnabled(!m); },
  });
  return api;
})();
