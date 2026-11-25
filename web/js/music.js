// Garden Guard web: background music (owner: Bobby). Sits next to audio.js (Sfx, sound effects).
// Original tracks by Dale in audio/ (MP3), played with Web Audio:
//   - the two loops (build, wave) are decoded ONCE into AudioBuffers and played as looped
//     AudioBufferSourceNodes with explicit loop points from audio/music.json (an <audio> tag, or looping
//     the whole file, leaves a gap or a click at the seam: MP3 decoders trim the encoder delay differently);
//   - a 1.5 s equal-power crossfade moves between them when the phase changes;
//   - the win/lose stingers play over the loop, which is ducked while they sound;
//   - nothing starts before the first user gesture; the context is suspended while the tab is hidden.
// Music settings are SEPARATE from the sound effects: on/off and volume, saved in localStorage
// ("ggweb:music" = 1/0, "ggweb:musicVolume" = 0..1), default ON at 0.6; the gain is 0.1 x volume.
// audio/music.json (Dale): { "build": { "loopStart": 0.5, "loopEnd": 65.95, "loop": 65.45, "bpm": 88 }, "wave": {...},
//   "win": {...}, "lose": {...} }, in seconds; optional "file" per track. The loop files hold 0.5 s of lead-in and
// a wrapped tail around the window, so playback starts AT loopStart (bar 1) and loops loopStart..loopEnd.
// API: Music.unlock() (every gesture; cheap), Music.setPhase('build'|'wave'), Music.stinger('win'|'lose', hold),
//      Music.release() (a new run begins: bring a held loop back), Music.enabled() / setEnabled(on),
//      Music.volume() / setVolume(0..1), Music.state() (for tests).
// Failure (no Web Audio, files missing offline, undecodable) means silence, never an exception.
var Music = (function () {
  'use strict';

  var KEY_ON = 'ggweb:music', KEY_VOL = 'ggweb:musicVolume';
  var DEFAULT_VOL = 0.6;      // slider position 0..1
  var BASE_GAIN = 0.1;        // -20 dB: the files are mastered near -20 LUFS and the sound effects are very quiet (peaks near
                              // -30 dBFS); gain = BASE_GAIN x volume (linear). Tune the level here only (Hank's ruling).
  var BASE = 'audio/';
  var TRACKS = {
    build: { file: 'music-build.mp3', loop: true },
    wave: { file: 'music-wave.mp3', loop: true },
    win: { file: 'stinger-win.mp3', loop: false },
    lose: { file: 'stinger-lose.mp3', loop: false },
  };
  var FADE = 1.5;             // seconds: crossfade between loops, first fade-in, the loop coming back after a stinger
  var OFF_FADE = 0.3;         // seconds: turning music off
  var DUCK_GAIN = 0.25, DUCK_DOWN = 0.2;   // loop level while a stinger plays, and how fast it gets there
  var STINGER_MAX_WAIT_MS = 1500;   // a stinger whose file isn't decoded yet within this long is skipped

  var enabled = true, volume = DEFAULT_VOL;
  try {
    var s = localStorage.getItem(KEY_ON); if (s !== null) enabled = s !== '0';
    var v = parseFloat(localStorage.getItem(KEY_VOL)); if (isFinite(v)) volume = Math.max(0, Math.min(1, v));
  } catch (e) { /* private mode: defaults */ }

  var ctx = null, failed = false, master = null, duck = null;
  var gestured = false;                 // true once unlock() has run inside a gesture
  var want = 'build';                   // the loop the game wants right now
  var cur = null;                       // { name, src, gain }: the loop that is playing
  var buffers = {};                     // name -> AudioBuffer (decoded once)
  var loading = {};                     // name -> Promise (fetch + decode in flight)
  var lost = {};                        // name -> true when it could not be loaded (retried on the next unlock)
  var meta = {};                        // audio/music.json
  var manifestTried = false;
  var preloadStarted = false;
  var held = false;                     // a Won/Lost stinger keeps the loop ducked until release()
  var stingerUntil = 0;                 // ctx time when the current stinger ends (0 = none)

  function running() { return !!ctx && ctx.state === 'running'; }
  function gainFor(x) { return BASE_GAIN * x; }
  function fileOf(name) { return (meta[name] && meta[name].file) || TRACKS[name].file; }

  function ensureCtx() {
    if (ctx || failed) return ctx;
    try {
      var AC = window.AudioContext || window.webkitAudioContext;
      if (!AC) { failed = true; return null; }
      ctx = new AC();
      master = ctx.createGain(); master.gain.value = gainFor(volume);
      duck = ctx.createGain(); duck.gain.value = 1;
      duck.connect(master); master.connect(ctx.destination);
    } catch (e) { failed = true; ctx = null; }
    return ctx;
  }

  // ---------- loading: fetch + decodeAudioData, once per track ----------
  function decode(ab) {
    return new Promise(function (res, rej) {
      var p = ctx.decodeAudioData(ab, res, rej);   // callback form too: older Safari has no promise form
      if (p && p.then) p.then(res, rej);
    });
  }
  function load(name) {
    if (buffers[name]) return Promise.resolve(buffers[name]);
    if (loading[name]) return loading[name];
    if (!ctx || !TRACKS[name]) return Promise.resolve(null);
    loading[name] = loadManifest().then(function () { return fetch(BASE + fileOf(name)); })
      .then(function (r) { if (!r.ok) throw new Error('HTTP ' + r.status); return r.arrayBuffer(); })
      .then(decode)
      .then(function (b) { buffers[name] = b; delete lost[name]; delete loading[name]; return b; })
      .catch(function () { lost[name] = true; delete loading[name]; return null; });   // silence, never an error
    return loading[name];
  }
  function loadManifest() {
    if (manifestTried) return Promise.resolve();
    manifestTried = true;
    return fetch(BASE + 'music.json').then(function (r) { return r.ok ? r.json() : null; })
      .then(function (j) { if (j && typeof j === 'object') meta = j; }).catch(function () { /* optional file */ });
  }
  // Lazy order: the build loop first (it is what plays), then the wave loop, then the stingers, one at a time,
  // so phones don't decode four files at once while the game starts.
  function preload() {
    if (preloadStarted) return;
    preloadStarted = true;
    load('build').then(function () { apply(); return load('wave'); })
      .then(function () { return load('win'); }).then(function () { return load('lose'); }).catch(function () {});
  }
  // Loop window in seconds: the manifest's loopStart/loopEnd (or loopStart 0.5 + "loop"); else the whole buffer.
  function windowOf(name, b) {
    var m = meta[name] || {}, a = 0, z = b.duration;
    if (m.loopStart >= 0 && m.loopEnd > m.loopStart) { a = m.loopStart; z = m.loopEnd; }
    else if (m.loop > 0) { a = 0.5; z = 0.5 + m.loop; }
    if (z > b.duration || a >= z) { a = 0; z = b.duration; }   // a window that doesn't fit the file: play it all
    return { a: a, z: z };
  }

  // ---------- loops ----------
  function curve(up) {   // equal-power fade curve
    var n = 32, a = new Float32Array(n);
    for (var i = 0; i < n; i++) { var x = i / (n - 1); a[i] = up ? Math.sin(x * Math.PI / 2) : Math.cos(x * Math.PI / 2); }
    return a;
  }
  function fade(gainNode, up, t0, dur) {
    var g = gainNode.gain;
    try {
      g.cancelScheduledValues(t0);
      g.setValueAtTime(up ? 0 : 1, t0);
      g.setValueCurveAtTime(curve(up), t0, dur);
    } catch (e) { g.setValueAtTime(up ? 1 : 0, t0 + dur); }
  }
  function startLoop(name, fadeIn) {
    var b = buffers[name];
    if (!b || !running()) return null;
    var w = windowOf(name, b), src = ctx.createBufferSource(), g = ctx.createGain();
    src.buffer = b; src.loop = true; src.loopStart = w.a; src.loopEnd = w.z;
    src.connect(g); g.connect(duck);
    var t0 = ctx.currentTime + 0.02;
    fade(g, true, t0, fadeIn);
    src.start(t0, w.a);   // begin ON bar 1, not at the start of the file
    return { name: name, src: src, gain: g };
  }
  function stopLoop(l, fadeOut) {
    if (!l) return;
    var t0 = ctx.currentTime;
    fade(l.gain, false, t0, fadeOut);
    try { l.src.stop(t0 + fadeOut + 0.05); } catch (e) { /* already stopped */ }
    setTimeout(function () { try { l.src.disconnect(); l.gain.disconnect(); } catch (e) { /* */ } }, (fadeOut + 0.3) * 1000);
  }
  // Make the playing loop match `want` (crossfade). Called on phase changes, after loads and on resume.
  // force: start `want` again from bar 1 even if it is already playing (a new run).
  function apply(force) {
    if (!enabled || !running()) return;
    if (cur && cur.name === want && !force) return;
    if (!buffers[want]) { load(want).then(function () { apply(force); }); return; }   // keep what plays until it is decoded
    var old = cur;
    cur = startLoop(want, FADE);
    if (old) stopLoop(old, FADE);
  }

  // ---------- public ----------
  // Safe to call on every gesture: a no-op while running. iOS only allows audio inside touchend/click.
  function unlock() {
    if (!enabled) { gestured = true; return; }
    if (running()) { gestured = true; return; }
    if (!ensureCtx()) return;
    gestured = true;
    try {
      if (ctx.resume) { var pr = ctx.resume(); if (pr && pr.then) pr.then(function () { apply(); }, function () {}); }
      var b = ctx.createBuffer(1, 1, ctx.sampleRate), src = ctx.createBufferSource();   // iOS: a silent sound in the gesture
      src.buffer = b; src.connect(ctx.destination); src.start(0);
    } catch (e) { /* silence */ }
    for (var k in lost) delete lost[k];   // a new gesture is a new chance (e.g. the network came back)
    preload();
    apply();
  }

  function setPhase(p) {
    var w = p === 'wave' ? 'wave' : 'build';
    if (w === want) return;
    want = w;
    apply();
  }

  // Play a stinger over the loop, ducking the loop. hold = the run is over (Won/Lost): the loop stays ducked
  // until release(); otherwise it comes back over FADE as the stinger rings out.
  function stinger(name, hold) {
    if (!enabled || !gestured || !running() || !TRACKS[name] || TRACKS[name].loop) return;
    var t = Date.now();
    function play(b) {
      if (!b || !enabled || !running()) return;
      if (Date.now() - t > STINGER_MAX_WAIT_MS) return;   // too late to be a stinger
      var src = ctx.createBufferSource();
      src.buffer = b; src.connect(master);   // not through the duck: it must stay clear above the loop
      var t0 = ctx.currentTime + 0.02, end = t0 + b.duration;
      stingerUntil = Math.max(stingerUntil, end);
      held = !!hold;
      duck.gain.cancelScheduledValues(t0);
      duck.gain.setValueAtTime(duck.gain.value, t0);
      duck.gain.linearRampToValueAtTime(DUCK_GAIN, t0 + DUCK_DOWN);
      if (!held) {
        duck.gain.setValueAtTime(DUCK_GAIN, Math.max(t0 + DUCK_DOWN, end));
        duck.gain.linearRampToValueAtTime(1, Math.max(t0 + DUCK_DOWN, end) + FADE);
      }
      src.start(t0);
    }
    if (buffers[name]) play(buffers[name]); else load(name).then(play);
  }

  // A new run starts after a Won/Lost stinger: the loop comes back up, from bar 1.
  function release() {
    if (!held) return;
    held = false;
    if (!ctx || !duck) return;
    var t0 = ctx.currentTime;
    duck.gain.cancelScheduledValues(t0);
    duck.gain.setValueAtTime(duck.gain.value, t0);
    duck.gain.linearRampToValueAtTime(1, t0 + FADE);
    apply(true);
  }

  function setEnabled(on) {
    on = !!on;
    if (on === enabled) return;
    enabled = on;
    try { localStorage.setItem(KEY_ON, enabled ? '1' : '0'); } catch (e) { /* private mode */ }
    if (enabled) { unlock(); apply(); return; }
    if (ctx && cur) { stopLoop(cur, OFF_FADE); cur = null; }
  }

  function setVolume(x) {
    volume = Math.max(0, Math.min(1, +x || 0));
    try { localStorage.setItem(KEY_VOL, String(volume)); } catch (e) { /* private mode */ }
    if (master) master.gain.setTargetAtTime(gainFor(volume), ctx.currentTime, 0.03);
  }

  // The tab is hidden: stop the clock (and the sound); back on screen: carry on from the same spot.
  document.addEventListener('visibilitychange', function () {
    if (!ctx) return;
    try {
      if (document.hidden) { if (ctx.state === 'running') ctx.suspend(); }
      else if (enabled && gestured) { var p = ctx.resume(); if (p && p.then) p.then(function () { apply(); }, function () {}); }
    } catch (e) { /* */ }
  });

  return {
    unlock: unlock, setPhase: setPhase, stinger: stinger, release: release,
    enabled: function () { return enabled; }, setEnabled: setEnabled,
    volume: function () { return volume; }, setVolume: setVolume,
    state: function () {   // for tests and the console
      return { enabled: enabled, volume: volume, ctx: ctx ? ctx.state : null, want: want, playing: cur ? cur.name : null,
        loaded: Object.keys(buffers), lost: Object.keys(lost), failed: failed, held: held, stingerUntil: stingerUntil,
        duck: duck ? duck.gain.value : null, master: master ? master.gain.value : null,
        loopWindow: cur && buffers[cur.name] ? windowOf(cur.name, buffers[cur.name]) : null };
    },
    _tracks: TRACKS,
  };
})();
