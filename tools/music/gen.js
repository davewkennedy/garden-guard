#!/usr/bin/env node
// Garden Guard music generator (owner: Dale). ORIGINAL music, composed and synthesized entirely by
// this script: no samples, no recordings, no third-party audio, no melodies from existing songs.
// Deterministic: the same script always renders the same samples (no Math.random; noise comes from
// fixed seeds per event), so the MP3s can be rebuilt and checked at any time.
//
//   node tools/music/gen.js [--out DIR]      -> DIR/*.wav (32-bit float, mono, 44.1 kHz) + layout.json
//   bash tools/music/build.sh                -> web/music/*.mp3 + web/music/music.json (loudness, encode)
//
// Same synth vocabulary as the game's SFX (web/js/audio.js): sine/triangle/square/saw oscillators,
// exponential decay envelopes, RBJ biquad filters, filtered noise; plus a small Schroeder reverb.
// Same key family as the SFX (C major; the wave loop is in the relative minor, A minor).
//
// SEAMLESS LOOPS: a loop of length L is rendered three times in a row (cycles 0, 1, 2) and the file
// holds [L - PRE, 2L + POST): from there on the signal is exactly L-periodic (every note tail and
// reverb tail from the previous cycle is already present). So the player can loop ANY window
// [s, s + L) inside the file. music.json gives loopStart = PRE and loopEnd = PRE + L; because the
// file is periodic around both points, an MP3 decoder that does or doesn't trim encoder padding
// (a few tens of ms) still loops seamlessly. Start playback at loopStart.
'use strict';
const fs = require('fs');
const path = require('path');

const RATE = 44100;
const PRE = 0.5, POST = 1.5;   // seconds kept before the loop start and after the loop end

// ------------------------------------------------------------------ synth
const mtof = m => 440 * Math.pow(2, (m - 69) / 12);

function biquad(type, freq, q) {
  const f = { b0: 1, b1: 0, b2: 0, a1: 0, a2: 0, x1: 0, x2: 0, y1: 0, y2: 0 };
  if (!type) return f;
  const w = 2 * Math.PI * Math.min(freq, RATE * 0.49) / RATE;
  const cw = Math.cos(w), alpha = Math.sin(w) / (2 * q), a0 = 1 + alpha;
  if (type === 'lp') { f.b0 = (1 - cw) / 2; f.b1 = 1 - cw; f.b2 = (1 - cw) / 2; }
  else if (type === 'hp') { f.b0 = (1 + cw) / 2; f.b1 = -(1 + cw); f.b2 = (1 + cw) / 2; }
  else { f.b0 = alpha; f.b1 = 0; f.b2 = -alpha; }
  f.b0 /= a0; f.b1 /= a0; f.b2 /= a0; f.a1 = -2 * cw / a0; f.a2 = (1 - alpha) / a0;
  return f;
}
function filt(f, s) {
  const y = f.b0 * s + f.b1 * f.x1 + f.b2 * f.x2 - f.a1 * f.y1 - f.a2 * f.y2;
  f.x2 = f.x1; f.x1 = s; f.y2 = f.y1; f.y1 = y;
  return y;
}
function osc(kind, phase) {
  if (kind === 'sine') return Math.sin(2 * Math.PI * phase);
  if (kind === 'tri') return 4 * Math.abs(phase - 0.5) - 1;
  if (kind === 'square') return phase < 0.5 ? 1 : -1;
  return 2 * phase - 1;   // saw
}
function lcg(seed) {   // same LCG as audio.js, seeded per event so every cycle repeats exactly
  let s = seed >>> 0;
  return () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return s / 2147483648 - 1; };
}

// One voice into dry/wet buses. o: {t, dur, f, f2 (glide end), wave, gain, att, rel ('exp' decay to
// -80 dB over dur | number = sustain then linear release seconds), lp, hp, q, vib, detune, wet, noise}
function voice(bus, o) {
  const start = (bus.off || 0) + Math.round(o.t * RATE), len = Math.round(o.dur * RATE);
  const att = o.att === undefined ? 0.005 : o.att;
  const fl = o.lp ? biquad('lp', o.lp, o.q || 0.707) : null;
  const fh = o.hp ? biquad('hp', o.hp, o.q || 0.707) : null;
  const fb = o.bp ? biquad('bp', o.bp, o.q || 1) : null;
  const rnd = o.noise ? lcg(o.noise) : null;
  const phases = [0, 0.25, 0.5];
  const det = o.detune || 0;   // cents: 3 oscillators -det, 0, +det
  const nOsc = det ? 3 : 1;
  for (let i = 0; i < len; i++) {
    const t = i / RATE;
    let env;
    if (t < att) env = t / att;
    else if (o.rel === undefined || o.rel === 'exp') env = Math.pow(1e-4, (t - att) / Math.max(1e-3, o.dur - att));
    else { const relStart = o.dur - o.rel; env = t < relStart ? 1 : Math.max(0, 1 - (t - relStart) / o.rel); }
    let s = 0;
    if (rnd) s = rnd();
    else {
      let fr = o.f2 ? o.f * Math.pow(o.f2 / o.f, Math.min(1, t / (o.glide || o.dur))) : o.f;
      if (o.vib) fr *= 1 + o.vib * Math.sin(2 * Math.PI * 5.2 * t) * Math.min(1, t / 0.3);
      for (let k = 0; k < nOsc; k++) {
        const c = nOsc === 3 ? (k - 1) * det : 0;
        phases[k] += fr * Math.pow(2, c / 1200) / RATE;
        phases[k] -= Math.floor(phases[k]);
        s += osc(o.wave, phases[k]) / nOsc;
      }
      if (o.h2) {   // a decaying 2nd/4th harmonic for bell/kalimba colour
        s += o.h2 * Math.sin(2 * Math.PI * phases[0] * (o.h2n || 2)) * Math.exp(-t * 9);
      }
    }
    if (fl) s = filt(fl, s);
    if (fh) s = filt(fh, s);
    if (fb) s = filt(fb, s);
    const v = s * env * o.gain;
    const j = start + i;
    if (j < 0 || j >= bus.dry.length) continue;
    bus.dry[j] += v;
    if (o.wet) bus.wet[j] += v * o.wet;
  }
}

// Small Schroeder reverb (4 damped combs + 2 allpasses), mono.
function reverb(x, mix) {
  const out = new Float64Array(x.length);
  const combs = [1116, 1188, 1277, 1356].map(d => ({ d, buf: new Float64Array(d), i: 0, lp: 0 }));
  const aps = [556, 441].map(d => ({ d, buf: new Float64Array(d), i: 0 }));
  for (let n = 0; n < x.length; n++) {
    let s = 0;
    for (const c of combs) {
      const y = c.buf[c.i];
      c.lp = y * 0.6 + c.lp * 0.4;
      c.buf[c.i] = x[n] + c.lp * 0.80;
      c.i = (c.i + 1) % c.d;
      s += y;
    }
    for (const a of aps) {
      const b = a.buf[a.i];
      const y = -s + b;
      a.buf[a.i] = s + b * 0.5;
      a.i = (a.i + 1) % a.d;
      s = y;
    }
    out[n] = s * mix * 0.25;
  }
  return out;
}

function newBus(seconds) {
  const n = Math.ceil(seconds * RATE);
  return { dry: new Float64Array(n), wet: new Float64Array(n), off: 0 };
}
function mixdown(bus, wetMix) {
  const r = reverb(bus.wet, wetMix);
  const out = new Float32Array(bus.dry.length);
  for (let i = 0; i < out.length; i++) out[i] = bus.dry[i] + r[i];
  return out;
}

// ------------------------------------------------------------------ instruments
const I = {
  kalimba: (b, t, m, vel) => voice(b, { t, dur: 1.1, f: mtof(m), wave: 'sine', gain: 0.22 * vel, att: 0.003,
                                       h2: 0.35, h2n: 4, wet: 0.5 }),
  bell: (b, t, m, beats, spb, vel = 1) => voice(b, { t, dur: Math.max(1.2, beats * spb + 0.8), f: mtof(m),
                                       wave: 'tri', gain: 0.17 * vel, att: 0.008, h2: 0.25, vib: 0.003,
                                       wet: 0.7 }),
  pad: (b, t, m, secs) => voice(b, { t, dur: secs + 0.9, f: mtof(m), wave: 'saw', detune: 9, gain: 0.035,
                                     att: 0.7, rel: 0.9, lp: 900, wet: 0.9 }),
  softBass: (b, t, m, secs, vel = 1) => voice(b, { t, dur: secs, f: mtof(m), wave: 'sine', gain: 0.34 * vel, att: 0.01,
                                          h2: 0.15, wet: 0.1 }),
  shaker: (b, t, seed, vel = 1) => voice(b, { t, dur: 0.06, wave: 'sine', noise: seed, hp: 6500, gain: 0.05 * vel,
                                              att: 0.004 }),
  kick: (b, t, g = 0.45) => { voice(b, { t, dur: 0.28, f: 150, f2: 46, glide: 0.12, wave: 'sine', gain: g, att: 0.002 });
                    voice(b, { t, dur: 0.02, wave: 'sine', noise: 777, lp: 2500, gain: 0.12, att: 0.001 }); },
  // (vel defaults to 1: the Sunny Meadow tracks render exactly as before)
  snare: (b, t, seed, vel = 1) => { voice(b, { t, dur: 0.17, wave: 'sine', noise: seed, bp: 1900, q: 0.8, gain: 0.32 * vel,
                                      att: 0.002, wet: 0.35 });
                           voice(b, { t, dur: 0.09, f: 210, f2: 165, wave: 'tri', gain: 0.18 * vel, att: 0.002 }); },
  hat: (b, t, seed, open, vel = 1) => voice(b, { t, dur: open ? 0.14 : 0.045, wave: 'sine', noise: seed, hp: 7500,
                                        gain: (open ? 0.07 : 0.06) * vel, att: 0.002 }),
  pumpBass: (b, t, m, secs) => voice(b, { t, dur: secs, f: mtof(m), wave: 'square', gain: 0.11, att: 0.004,
                                          lp: 520, q: 1.1, rel: 0.03 }),
  arp: (b, t, m) => voice(b, { t, dur: 0.16, f: mtof(m), wave: 'square', gain: 0.05, att: 0.002, lp: 2600,
                               wet: 0.3 }),
  lead: (b, t, m, secs, vel = 1) => { voice(b, { t, dur: secs + 0.12, f: mtof(m), wave: 'saw', detune: 6,
                                                 gain: 0.075 * vel, att: 0.012, rel: 0.1, lp: 2300, vib: 0.004,
                                                 wet: 0.45 });
                                      voice(b, { t, dur: secs + 0.12, f: mtof(m), wave: 'tri', gain: 0.07 * vel,
                                                 att: 0.012, rel: 0.1, wet: 0.45 }); },
  // ---- added for the map soundtracks (maps 2-10) ----
  // plucked string (Karplus-Strong): bright 0..1 = brighter attack; decay per pass (lower = shorter, "palm muted")
  pluck: (b, t, m, vel = 1, o = {}) => ks(b, { t, f: mtof(m), dur: o.dur || 1.2, gain: 0.3 * vel,
                                              bright: o.bright === undefined ? 0.5 : o.bright,
                                              decay: o.decay || 0.996, seed: 9000 + m * 7 + Math.round(t * 997),
                                              wet: o.wet === undefined ? 0.3 : o.wet }),
  flute: (b, t, m, secs, vel = 1) => { voice(b, { t, dur: secs + 0.1, f: mtof(m), wave: 'tri', gain: 0.12 * vel, att: 0.035,
                                                  rel: 0.1, vib: 0.005, lp: 4000, wet: 0.55 });
                                       voice(b, { t, dur: 0.07, wave: 'sine', noise: 4000 + m * 13 + Math.round(t * 991),
                                                  bp: Math.min(9000, mtof(m) * 2), q: 2, gain: 0.025 * vel, att: 0.01,
                                                  wet: 0.3 }); },
  whistle: (b, t, m, secs, vel = 1) => voice(b, { t, dur: secs + 0.1, f: mtof(m), wave: 'sine', gain: 0.13 * vel,
                                                  att: 0.03, rel: 0.09, vib: 0.008, wet: 0.5 }),
  musicBox: (b, t, m, vel = 1) => voice(b, { t, dur: 1.6, f: mtof(m), wave: 'sine', gain: 0.16 * vel, att: 0.002,
                                             h2: 0.45, h2n: 3, wet: 0.6 }),
  horn: (b, t, m, secs, vel = 1) => voice(b, { t, dur: secs + 0.15, f: mtof(m), wave: 'saw', detune: 7, gain: 0.06 * vel,
                                               att: 0.06, rel: 0.15, lp: 1400, vib: 0.003, wet: 0.6 }),
  fiddle: (b, t, m, secs, vel = 1) => voice(b, { t, dur: secs + 0.08, f: mtof(m), wave: 'saw', detune: 4, gain: 0.065 * vel,
                                                 att: 0.03, rel: 0.08, lp: 3200, vib: 0.007, wet: 0.45 }),
  // a sine that glides in from the previous note (theremin-like): from = previous midi note
  theremin: (b, t, m, secs, from) => voice(b, { t, dur: secs + 0.15, f: mtof(from), f2: mtof(m), glide: 0.08, wave: 'sine',
                                                gain: 0.14, att: 0.06, rel: 0.2, vib: 0.012, wet: 0.7 }),
  sqLead: (b, t, m, secs, vel = 1) => { voice(b, { t, dur: secs + 0.08, f: mtof(m), wave: 'square', gain: 0.045 * vel,
                                                   att: 0.01, rel: 0.06, lp: 2200, vib: 0.004, wet: 0.35 });
                                        voice(b, { t, dur: secs + 0.08, f: mtof(m), wave: 'tri', gain: 0.05 * vel,
                                                   att: 0.01, rel: 0.06, wet: 0.35 }); },
  padX: (b, t, m, secs, o = {}) => voice(b, { t, dur: secs + (o.rel || 0.9), f: mtof(m), wave: o.wave || 'saw', detune: 9,
                                               gain: o.gain || 0.03, att: o.att || 0.7, rel: o.rel || 0.9, lp: o.lp || 900,
                                               wet: 0.9 }),
  subBass: (b, t, m, secs, vel = 1) => voice(b, { t, dur: secs, f: mtof(m), wave: 'sine', gain: 0.32 * vel, att: 0.008,
                                                  rel: 0.04, wet: 0.05 }),
  bassoon: (b, t, m, secs, vel = 1) => { voice(b, { t, dur: secs, f: mtof(m), wave: 'square', gain: 0.1 * vel, att: 0.012,
                                                    rel: 0.05, lp: 650, q: 1, wet: 0.15 });
                                         voice(b, { t, dur: secs, f: mtof(m), wave: 'sine', gain: 0.16 * vel, att: 0.01,
                                                    rel: 0.05 }); },
  tom: (b, t, m, g = 0.3) => voice(b, { t, dur: 0.4, f: mtof(m) * 1.6, f2: mtof(m), glide: 0.08, wave: 'sine', gain: g,
                                        att: 0.002, wet: 0.2 }),
  timpani: (b, t, m, g = 0.35) => { voice(b, { t, dur: 1.4, f: mtof(m) * 1.08, f2: mtof(m), glide: 0.1, wave: 'sine', gain: g,
                                               att: 0.003, wet: 0.4 });
                                    voice(b, { t, dur: 0.08, wave: 'sine', noise: 6100 + m, lp: 300, gain: 0.1 * g / 0.35,
                                               att: 0.001 }); },
  rim: (b, t, seed, vel = 1) => { voice(b, { t, dur: 0.05, wave: 'sine', noise: seed, bp: 2600, q: 2, gain: 0.15 * vel,
                                             att: 0.001, wet: 0.2 });
                                  voice(b, { t, dur: 0.03, f: 400, wave: 'tri', gain: 0.06 * vel, att: 0.001 }); },
  block: (b, t, m, vel = 1) => voice(b, { t, dur: 0.07, f: mtof(m), wave: 'sine', gain: 0.12 * vel, att: 0.001, h2: 0.4,
                                          h2n: 3, wet: 0.25 }),
  clap: (b, t, seed, vel = 1) => [0, 0.011, 0.022].forEach((d, k) => voice(b, { t: t + d, dur: k === 2 ? 0.14 : 0.03,
                                          wave: 'sine', noise: seed + k, bp: 1500, q: 0.9, gain: 0.2 * vel, att: 0.001,
                                          wet: 0.4 })),
  sleigh: (b, t, seed, vel = 1) => [0, 0.011, 0.024, 0.04].forEach((d, k) => voice(b, { t: t + d, dur: 0.08, wave: 'sine',
                                          noise: seed * 4 + k, bp: 9000, q: 2.5, gain: 0.05 * vel, att: 0.001, wet: 0.3 })),
  cricket: (b, t) => [0, 0.035, 0.07].forEach((d) => voice(b, { t: t + d, dur: 0.025, f: 4400, wave: 'sine', gain: 0.02,
                                          att: 0.003, wet: 0.5 })),
  sparkle: (b, t, m, vel = 1) => voice(b, { t, dur: 0.6, f: mtof(m), wave: 'sine', gain: 0.035 * vel, att: 0.01, wet: 0.9 }),
  crash: (b, t, seed, vel = 1) => voice(b, { t, dur: 1.3, wave: 'sine', noise: seed, hp: 5000, gain: 0.07 * vel,
                                             att: 0.002, wet: 0.5 }),
};

// Karplus-Strong plucked string, tuned with a first-order allpass (fractional delay). Deterministic: the
// excitation is seeded noise, so every cycle of a loop is identical.
function ks(bus, o) {
  const P = RATE / o.f, N = Math.max(2, Math.floor(P - 0.5)), d = P - 0.5 - N, C = (1 - d) / (1 + d);
  const len = Math.round(o.dur * RATE), y = new Float64Array(len);
  const rnd = lcg(o.seed), n0 = Math.min(N, len);
  let lp = 0, mean = 0;
  for (let i = 0; i < n0; i++) { lp += o.bright * (rnd() - lp); y[i] = lp; mean += lp / n0; }
  for (let i = 0; i < n0; i++) y[i] -= mean;   // no DC in the string
  let ax = 0, ay = 0;
  for (let i = N; i < len; i++) {
    const a = 0.5 * (y[i - N] + (i - N - 1 >= 0 ? y[i - N - 1] : 0)) * o.decay;
    const z = C * a + ax - C * ay;
    ax = a; ay = z; y[i] = z;
  }
  const start = (bus.off || 0) + Math.round(o.t * RATE), fade = Math.min(len, Math.round(0.02 * RATE));
  for (let i = 0; i < len; i++) {
    const j = start + i;
    if (j < 0 || j >= bus.dry.length) continue;
    const v = y[i] * (i >= len - fade ? (len - i) / fade : 1) * o.gain;
    bus.dry[j] += v;
    if (o.wet) bus.wet[j] += v * o.wet;
  }
}

// ------------------------------------------------------------------ compositions
// Melodies are [beatInBar, beats, midi]; chords are { root (bass midi), tones (midi) }.

// BUILD: calm garden loop. C major, 88 BPM, 24 bars (3 x 8), about 65 s.
function buildLoop(bus, t0) {
  const bpm = 88, spb = 60 / bpm, bar = 4 * spb;
  const chords = [
    { root: 48, tones: [64, 67, 71, 74] },   // Cmaj9
    { root: 45, tones: [64, 67, 69, 72] },   // Am7
    { root: 41, tones: [65, 69, 72, 76] },   // Fmaj7
    { root: 43, tones: [62, 67, 71, 76] },   // G6
    { root: 40, tones: [62, 64, 67, 71] },   // Em7
    { root: 45, tones: [64, 67, 69, 72] },   // Am7
    { root: 50, tones: [65, 69, 72, 76] },   // Dm9
    { root: 43, tones: [62, 67, 72, 74] },   // Gsus4
  ];
  const m1 = [
    [[0, 1.5, 76], [1.5, 0.5, 74], [2, 2, 79]],
    [[0, 1, 76], [1, 1, 72], [2, 2, 69]],
    [[0, 1.5, 72], [1.5, 0.5, 74], [2, 1, 76], [3, 1, 81]],
    [[0, 3, 79]],
    [[0, 1.5, 71], [1.5, 0.5, 74], [2, 2, 76]],
    [[0, 1, 79], [1, 1, 76], [2, 1, 74], [3, 1, 72]],
    [[0, 1.5, 74], [1.5, 0.5, 77], [2, 1, 81], [3, 1, 79]],
    [[0, 2, 74], [2, 2, 71]],
  ];
  const m2 = [
    [[0, 1, 67], [1, 1, 72], [2, 2, 76]],
    [[0, 1, 74], [1, 1, 72], [2, 2, 69]],
    [[0, 1, 69], [1, 1, 72], [2, 1.5, 77], [3.5, 0.5, 76]],
    [[0, 3, 74]],
    [[0, 1, 76], [1, 1, 79], [2, 2, 83]],
    [[0, 1.5, 81], [1.5, 0.5, 79], [2, 2, 76]],
    [[0, 1, 77], [1, 1, 76], [2, 1, 74], [3, 1, 72]],
    [[0, 2, 74], [2, 2, 67]],
  ];
  const arpA = [0, 2, 3, 1, 2, 3, 1, 2], arpC = [0, 2, 1, 3, 0, 2, 1, 3];
  for (let b = 0; b < 24; b++) {
    const c = chords[b % 8], tb = t0 + b * bar, sec = Math.floor(b / 8);
    c.tones.forEach(m => I.pad(bus, tb, m, bar));
    I.softBass(bus, tb, c.root, 2 * spb);
    I.softBass(bus, tb + 2 * spb, c.root + 7 > 52 ? c.root - 5 : c.root + 7, 1.6 * spb);
    const pat = sec === 2 ? arpC : arpA;
    for (let e = 0; e < 8; e++) {
      if (sec === 2 && (e === 3 || e === 7)) continue;   // section C: a little more air
      I.kalimba(bus, tb + e * spb / 2, c.tones[pat[e]], e % 2 ? 0.62 : 0.85);
    }
    if (b % 8 >= 4 || sec > 0)
      for (let e = 0; e < 4; e++) I.shaker(bus, tb + (e + 0.5) * spb, 1000 + b * 8 + e, e % 2 ? 0.7 : 1);
    const mel = sec === 1 ? m1 : sec === 2 ? m2 : null;
    if (mel) mel[b % 8].forEach(([at, beats, m]) => I.bell(bus, tb + at * spb, m, beats, spb));
  }
  return 24 * bar;
}

// WAVE: upbeat but not frantic. A minor (relative of C), 132 BPM (3:2 of 88), 32 bars, about 58 s.
function waveLoop(bus, t0) {
  const bpm = 132, spb = 60 / bpm, bar = 4 * spb, s16 = spb / 4;
  const chords = [
    { root: 45, tones: [69, 72, 76, 81] },   // Am
    { root: 41, tones: [69, 72, 77, 81] },   // F
    { root: 48, tones: [67, 72, 76, 79] },   // C
    { root: 43, tones: [67, 71, 74, 79] },   // G
    { root: 45, tones: [69, 72, 76, 81] },   // Am
    { root: 41, tones: [69, 72, 77, 81] },   // F
    { root: 50, tones: [69, 74, 77, 81] },   // Dm
    { root: 40, tones: [68, 71, 76, 80] },   // E (G# leads home to A)
  ];
  const w1 = [
    [[0, 0.5, 69], [0.5, 0.5, 72], [1, 1, 76], [2, 0.5, 74], [2.5, 0.5, 72], [3, 1, 74]],
    [[0, 1.5, 72], [1.5, 0.5, 69], [2, 1, 65], [3, 1, 69]],
    [[0, 0.5, 67], [0.5, 0.5, 72], [1, 1, 76], [2, 1, 79], [3, 1, 76]],
    [[0, 2, 74], [2, 1, 71], [3, 1, 74]],
    [[0, 1, 81], [1, 0.5, 79], [1.5, 0.5, 76], [2, 1, 74], [3, 1, 72]],
    [[0, 0.5, 72], [0.5, 0.5, 74], [1, 1, 72], [2, 2, 69]],
    [[0, 1, 77], [1, 1, 76], [2, 1, 74], [3, 1, 69]],
    [[0, 2, 68], [2, 2, 71]],
  ];
  const w2 = [
    [[0, 0.75, 76], [0.75, 0.75, 76], [1.5, 0.5, 81], [2, 1, 79], [3, 1, 76]],
    [[0, 0.75, 77], [0.75, 0.75, 76], [1.5, 0.5, 72], [2, 2, 69]],
    [[0, 0.75, 79], [0.75, 0.75, 79], [1.5, 0.5, 84], [2, 1, 83], [3, 1, 79]],
    [[0, 1.5, 86], [1.5, 0.5, 83], [2, 2, 79]],
    [[0, 0.75, 81], [0.75, 0.75, 79], [1.5, 0.5, 76], [2, 1, 79], [3, 1, 81]],
    [[0, 1, 84], [1, 1, 81], [2, 1, 77], [3, 1, 81]],
    [[0, 0.75, 86], [0.75, 0.75, 84], [1.5, 0.5, 81], [2, 1, 77], [3, 1, 74]],
    [[0, 2, 76], [2, 1, 80], [3, 1, 83]],
  ];
  const arpPat = [0, 2, 1, 3, 2, 0, 3, 1];
  for (let b = 0; b < 32; b++) {
    const c = chords[b % 8], tb = t0 + b * bar, sec = Math.floor(b / 8);
    // drums
    I.kick(bus, tb); I.kick(bus, tb + 2 * spb);
    if (sec === 2) I.kick(bus, tb + 2.5 * spb + (b % 2 ? 0 : spb));
    const snare = sec !== 3 && (sec > 0 || b >= 4);
    if (snare) { I.snare(bus, tb + spb, 3000 + b * 4); I.snare(bus, tb + 3 * spb, 3001 + b * 4); }
    if (sec !== 3 || b % 2)
      for (let e = 0; e < 8; e++) I.hat(bus, tb + e * spb / 2, 5000 + b * 8 + e, sec === 2 && e % 2 === 1);
    if (b === 31) for (let e = 8; e < 16; e++) I.snare(bus, tb + e * s16, 7000 + e);   // fill back to bar 1
    // bass: octave pump in eighths
    for (let e = 0; e < 8; e++) I.pumpBass(bus, tb + e * spb / 2, c.root + (e % 2 ? 12 : 0), spb / 2 * 0.9);
    // arp in sixteenths (sparser in section 1 so the lead has room later)
    for (let e = 0; e < 16; e++) {
      if (sec === 0 && e % 2) continue;
      I.arp(bus, tb + e * s16, c.tones[arpPat[e % 8]] + (sec === 2 && e >= 8 ? 12 : 0));
    }
    const mel = sec === 1 ? w1 : sec === 2 ? w2 : null;
    if (mel) mel[b % 8].forEach(([at, beats, m]) => I.lead(bus, tb + at * spb, m, beats * spb * 0.92));
    if (sec === 3) {   // breakdown: kalimba counter-line on chord tones
      [[0, 3], [1.5, 2], [2, 1], [3, 0]].forEach(([at, k]) => I.kalimba(bus, tb + at * spb, c.tones[k] + 12, 0.8));
    }
  }
  return 32 * bar;
}

// WIN: bright C major rise into an open chord, about 4 s.
function winStinger(bus, t0) {
  const step = 0.095;
  [67, 72, 76, 79, 84].forEach((m, i) => I.kalimba(bus, t0 + i * step, m, 1));
  const tc = t0 + 5 * step;
  // the chord rolls (20 ms apart) instead of striking at once: same sound, lower peak, so it can be louder
  [60, 64, 67, 72, 74, 79].forEach((m, i) => I.bell(bus, tc + 0.02 * i, m, 4, 0.6, 0.75));
  I.softBass(bus, tc, 48, 2.4);
  [64, 67, 72].forEach(m => I.pad(bus, tc, m, 1.8));
  [96, 100, 103].forEach((m, i) => voice(bus, { t: tc + 0.08 * i, dur: 0.7, f: mtof(m), wave: 'tri', gain: 0.03,
                                                 att: 0.004, wet: 0.8 }));
  I.kick(bus, tc, 0.25);
  return 4.2;
}

// LOSE: falling A minor line onto a low chord that droops and darkens, about 3 s.
function loseStinger(bus, t0) {
  [[0, 76], [0.26, 72], [0.52, 69]].forEach(([at, m]) => I.bell(bus, t0 + at, m, 0.4, 0.6, 0.9));
  const tc = t0 + 0.8;
  [57, 60, 64].forEach(m => voice(bus, { t: tc, dur: 2.1, f: mtof(m), f2: mtof(m - 0.6), wave: 'saw', detune: 8,
                                         gain: 0.06, att: 0.05, lp: 1100, rel: 1.2, wet: 0.7 }));
  voice(bus, { t: tc, dur: 2.2, f: mtof(45), f2: mtof(44.4), wave: 'sine', gain: 0.4, att: 0.01, wet: 0.2 });
  voice(bus, { t: tc, dur: 2.0, f: mtof(64), f2: mtof(63.4), wave: 'tri', gain: 0.1, att: 0.02, vib: 0.006,
               rel: 1.0, wet: 0.6 });
  return 3.1;
}

// ------------------------------------------------------------------ map soundtracks (maps 2-10)
// Sunny Meadow (map 1) keeps the build/wave loops above. Every other map has its own build loop and wave
// loop, written for its season and mood. Notation: mel('E5:1.5 D5:.5 G5:2 | ...', beatsPerBar) is one bar
// per '|', notes as name+octave with an optional :beats (default 1), 'r' for a rest; every bar must add up
// to beatsPerBar (checked). prog('G2 B3 D4 G4 | ...') is one chord per '|': the bass note, then the tones.
const NOTE = { C: 0, D: 2, E: 4, F: 5, G: 7, A: 9, B: 11 };
function nm(s) {
  const m = /^([A-G])(#|b)?(-?\d)$/.exec(s);
  if (!m) throw new Error('bad note ' + s);
  return NOTE[m[1]] + (m[2] === '#' ? 1 : m[2] === 'b' ? -1 : 0) + 12 * (+m[3] + 1);
}
function mel(str, bpb = 4) {
  return str.split('|').map((bar, i) => {
    let at = 0;
    const out = [];
    for (const tok of bar.trim().split(/\s+/).filter(Boolean)) {
      const [n, d] = tok.split(':'), beats = d === undefined ? 1 : parseFloat(d);
      if (n !== 'r') out.push([at, beats, nm(n)]);
      at += beats;
    }
    if (Math.abs(at - bpb) > 1e-9) throw new Error(`bar ${i + 1} has ${at} beats, not ${bpb}: ${bar}`);
    return out;
  });
}
function prog(str) {
  return str.split('|').map((c) => { const ns = c.trim().split(/\s+/).map(nm); return { root: ns[0], tones: ns.slice(1) }; });
}
// Plays a melody (bars from mel()) from bar firstBar: fn(t, midi, seconds, previousMidi).
function play(bars, t0, spb, bpb, firstBar, fn) {
  let prev = null;
  const all = [];
  bars.forEach((bar, i) => bar.forEach(([at, beats, m]) => all.push([t0 + (firstBar + i) * bpb * spb + at * spb, m, beats * spb])));
  prev = all.length ? all[all.length - 1][1] : 0;   // the loop's last note leads into its first
  for (const [t, m, s] of all) { fn(t, m, s, prev); prev = m; }
}
const fillSnare = (bus, t, s16, n, seed, vel = 0.8) => { for (let e = 0; e < n; e++) I.snare(bus, t + e * s16, seed + e, vel * (0.6 + 0.4 * e / n)); };

// 2 APPLE ORCHARD (late spring): a waltz. G major, 3/4, 100 BPM, 16 bars.
function orchardBuild(bus, t0) {
  const bpm = 100, spb = 60 / bpm, bpb = 3, bar = bpb * spb;
  const ch = prog('G2 B3 D4 G4 | F#2 A3 D4 F#4 | E2 G3 B3 E4 | C3 C4 E4 G4 | G2 B3 D4 G4 | A2 A3 C4 E4 | D3 A3 D4 F#4 | D3 A3 C4 F#4');
  const mA = mel('B4:2 D5 | A4:1.5 G4:.5 F#4 | G4 B4 E5 | E5:2 D5 | B4 D5 G5 | A5:1.5 G5:.5 E5 | F#5 E5 D5 | C5:2 A4', 3);
  const mB = mel('D5 G5 B5 | A5:2 F#5 | G5 E5 B4 | C5:2 E5 | D5:1.5 B4:.5 G4 | C5 E5 A5 | F#5:1.5 E5:.5 D5 | A4:2 F#4', 3);
  for (let b = 0; b < 16; b++) {
    const c = ch[b % 8], tb = t0 + b * bar;
    I.softBass(bus, tb, c.root, spb * 0.95);   // oom ...
    for (const k of [1, 2]) c.tones.slice(0, 3).forEach((m, j) => I.pluck(bus, tb + k * spb + j * 0.012, m, 0.5,
                                                                            { bright: 0.35, dur: 0.6 }));   // ... pa-pa
    c.tones.forEach((m) => I.pad(bus, tb, m, bar));
    if (b >= 8) I.musicBox(bus, tb + 2 * spb, c.tones[2] + 24, 0.4);
  }
  play(mA, t0, spb, bpb, 0, (t, m, s) => I.flute(bus, t, m, s * 0.95));
  play(mB, t0, spb, bpb, 8, (t, m, s) => I.flute(bus, t, m, s * 0.95));
  return 16 * bar;
}
// G major, 140 BPM, 16 bars: a bouncy country two-beat with a flute tune.
function orchardWave(bus, t0) {
  const bpm = 140, spb = 60 / bpm, bar = 4 * spb, s16 = spb / 4;
  const ch = prog('G2 G4 B4 D5 | C3 G4 C5 E5 | G2 G4 B4 D5 | D3 F#4 A4 D5 | E2 G4 B4 E5 | C3 G4 C5 E5 | D3 F#4 A4 D5 | D3 F#4 A4 C5');
  const mA = mel('D5:.5 E5:.5 G5 B5 G5 | E5 G5 C6:1.5 B5:.5 | B5 A5:.5 G5:.5 D5:2 | F#5 G5:.5 A5:.5 D5:2 | ' +
                 'E5:.5 F#5:.5 G5 B5 E6 | C6 B5 A5 G5 | A5:1.5 G5:.5 F#5 E5 | D5 F#5 A5:2');
  const mB = mel('G5:1.5 B5:.5 D6:2 | E6 D6:.5 C6:.5 G5:2 | B5:.5 C6:.5 D6 B5 G5 | A5:3 r | B5 G5 E5 G5 | C6:1.5 B5:.5 A5 E5 | ' +
                 'F#5 A5 D6 C6 | B5:.5 A5:.5 F#5 D5 r');
  for (let b = 0; b < 16; b++) {
    const c = ch[b % 8], tb = t0 + b * bar;
    I.kick(bus, tb, 0.4); I.kick(bus, tb + 2 * spb, 0.4);
    I.snare(bus, tb + spb, 2000 + b * 4, 0.6); if (b !== 15) I.snare(bus, tb + 3 * spb, 2001 + b * 4, 0.6);
    for (let e = 0; e < 8; e++) I.hat(bus, tb + e * spb / 2, 2200 + b * 8 + e, false, e % 2 ? 0.6 : 1);
    if (b === 15) fillSnare(bus, tb + 3 * spb, s16, 4, 2400);
    const fifth = c.root + 7 > 52 ? c.root - 5 : c.root + 7;
    I.pluck(bus, tb, c.root + 12, 1, { bright: 0.3, dur: 0.5, decay: 0.994 }); I.softBass(bus, tb, c.root, spb * 0.9, 0.8);
    I.pluck(bus, tb + 2 * spb, fifth + 12, 1, { bright: 0.3, dur: 0.5, decay: 0.994 }); I.softBass(bus, tb + 2 * spb, fifth, spb * 0.9, 0.8);
    for (const k of [0.5, 1.5, 2.5, 3.5])
      c.tones.forEach((m, j) => I.pluck(bus, tb + k * spb + j * 0.006, m - 12, 0.35, { bright: 0.6, dur: 0.25, decay: 0.99 }));
  }
  play(mA, t0, spb, 4, 0, (t, m, s) => I.flute(bus, t, m, s * 0.9));
  play(mB, t0, spb, 4, 8, (t, m, s) => { I.flute(bus, t, m, s * 0.9); I.musicBox(bus, t, m + 12, 0.35); });
  return 16 * bar;
}

// 3 THORNY RIDGE (high summer, dry and hot): D dorian, 70 BPM, 8 bars. A drone, a picked guitar,
// hand drums, crickets and a lonely whistle.
function ridgeBuild(bus, t0) {
  const bpm = 70, spb = 60 / bpm, bar = 4 * spb;
  const ch = prog('D2 D3 A3 F4 | C2 C3 G3 E4 | D2 D3 A3 F4 | G2 G3 B3 D4 | F2 F3 C4 A4 | C2 C3 G3 E4 | G2 G3 B3 D4 | A2 A3 C#4 E4');
  const m = mel('r:2 A4 D5 | E5:2 D5 C5 | D5:3 r | B4 D5 G5:1.5 F5:.5 | F5:2 E5 C5 | E5:1.5 D5:.5 C5 G4 | B4:2 D5 B4 | C#5:2 E5 r');
  const pat = [0, 1, 2, 3, 2, 1, 2, 1];
  for (let b = 0; b < 8; b++) {
    const c = ch[b], tb = t0 + b * bar, arr = [c.tones[0], c.tones[1], c.tones[2], c.tones[1] + 12];
    I.subBass(bus, tb, c.root, bar * 0.98, 0.55);
    for (let e = 0; e < 8; e++) I.pluck(bus, tb + e * spb / 2, arr[pat[e]], e % 2 ? 0.45 : 0.65, { bright: 0.55, decay: 0.995 });
    I.tom(bus, tb, 50, 0.3); I.tom(bus, tb + 1.5 * spb, 50, 0.18); I.tom(bus, tb + 2 * spb, 55, 0.22);
    I.tom(bus, tb + 3.5 * spb, 57, 0.15);
    if (b % 2) { I.cricket(bus, tb + 2.25 * spb); I.cricket(bus, tb + 3.25 * spb); }
  }
  play(m, t0, spb, 4, 0, (t, n, s) => I.whistle(bus, t, n, s * 0.95));
  return 8 * bar;
}
// D minor, 144 BPM, 16 bars: a galloping palm-muted riff, toms and a square lead.
function ridgeWave(bus, t0) {
  const bpm = 144, spb = 60 / bpm, bar = 4 * spb, s16 = spb / 4;
  const ch = prog('D2 D4 F4 A4 | D2 D4 F4 A4 | Bb1 D4 F4 Bb4 | C2 E4 G4 C5 | D2 D4 F4 A4 | D2 D4 F4 A4 | Bb1 D4 F4 Bb4 | A1 C#4 E4 A4');
  const lead = mel('D5:.5 F5:.5 A5 G5:.5 F5:.5 E5 | F5:1.5 E5:.5 D5:2 | D5:.5 F5:.5 Bb5 A5 F5 | G5:1.5 F5:.5 E5 C5 | ' +
                   'A5 A5:.5 G5:.5 F5 A5 | D6:2 C6 A5 | Bb5 A5:.5 G5:.5 F5 D5 | E5 C#5 A4 E5');
  const riff = [1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 0, 1, 0, 1, 1];
  for (let b = 0; b < 16; b++) {
    const c = ch[b % 8], tb = t0 + b * bar;
    I.kick(bus, tb); I.kick(bus, tb + 2 * spb); if (b % 2) I.kick(bus, tb + 2.5 * spb, 0.3);
    I.snare(bus, tb + spb, 3100 + b * 2, 0.8); if (b !== 15) I.snare(bus, tb + 3 * spb, 3101 + b * 2, 0.8);
    I.tom(bus, tb + 3.5 * spb, 52, 0.2); I.tom(bus, tb + 3.75 * spb, 47, 0.2);
    for (let e = 0; e < 8; e++) I.shaker(bus, tb + e * spb / 2, 3300 + b * 8 + e, e % 2 ? 0.8 : 1.2);
    if (b === 15) fillSnare(bus, tb + 3 * spb, s16, 4, 3500);
    for (let e = 0; e < 16; e++) {
      if (!riff[e]) continue;
      const acc = e % 4 === 0;
      I.pluck(bus, tb + e * s16, c.root + 12, acc ? 0.8 : 0.5, { bright: 0.4, dur: 0.14, decay: 0.98 });
      if (acc) I.pluck(bus, tb + e * s16, c.root + 19, 0.5, { bright: 0.4, dur: 0.14, decay: 0.98 });
    }
    I.subBass(bus, tb, c.root, 2 * spb * 0.95, 0.6); I.subBass(bus, tb + 2 * spb, c.root, 2 * spb * 0.95, 0.6);
  }
  play(lead.slice(0, 4), t0, spb, 4, 4, (t, m, s) => I.pluck(bus, t, m - 12, 0.7, { bright: 0.7, dur: s + 0.3 }));
  play(lead, t0, spb, 4, 8, (t, m, s) => I.sqLead(bus, t, m, s * 0.92));
  return 16 * bar;
}

// 4 HARVEST HOLLOW (late summer, golden fields): F major, 96 BPM, 12 bars. Banjo rolls, warm pad, flute.
function harvestBuild(bus, t0) {
  const bpm = 96, spb = 60 / bpm, bar = 4 * spb;
  const ch = prog('F2 F3 A3 C4 | Bb1 F3 Bb3 D4 | F2 F3 A3 C4 | C2 E3 G3 C4 | D2 F3 A3 D4 | Bb1 F3 Bb3 D4 | F2 F3 A3 C4 | ' +
                  'C2 E3 G3 C4 | Bb1 F3 Bb3 D4 | C2 E3 G3 C4 | D2 F3 A3 D4 | C2 E3 G3 Bb3');
  const fl = mel('A4 D5 F5:1.5 E5:.5 | D5:2 F5 D5 | C5:1.5 A4:.5 C5 F5 | E5:2 G5:2 | F5 D5 Bb4 D5 | E5:1.5 F5:.5 G5:2 | ' +
                 'A5 G5 F5 D5 | E5 C5 G4:2');
  const roll = [0, 1, 2, 0, 1, 2, 0, 1];
  for (let b = 0; b < 12; b++) {
    const c = ch[b], tb = t0 + b * bar, up = c.tones.map((m) => m + 12);
    for (let e = 0; e < 8; e++) I.pluck(bus, tb + e * spb / 2, up[roll[e]], e % 3 === 0 ? 0.55 : 0.4, { bright: 0.8, dur: 0.5, decay: 0.993 });
    I.softBass(bus, tb, c.root, 2 * spb); I.softBass(bus, tb + 2 * spb, c.root + 7 > 45 ? c.root - 5 : c.root + 7, 1.8 * spb);
    c.tones.forEach((m) => I.pad(bus, tb, m, bar));
    if (b >= 4) for (let e = 0; e < 4; e++) I.shaker(bus, tb + (e + 0.5) * spb, 4100 + b * 4 + e, 0.8);
    if (b >= 8) { I.musicBox(bus, tb, up[2] + 12, 0.4); I.musicBox(bus, tb + 2 * spb, up[1] + 12, 0.35); }
  }
  play(fl, t0, spb, 4, 4, (t, m, s) => I.flute(bus, t, m, s * 0.95));
  return 12 * bar;
}
// F major, 136 BPM, 16 bars: a hoedown two-step with a fiddle.
function harvestWave(bus, t0) {
  const bpm = 136, spb = 60 / bpm, bar = 4 * spb, s16 = spb / 4;
  const V = { F: 'F2 A3 C4 F4', Bb: 'Bb1 Bb3 D4 F4', C: 'C2 G3 C4 E4', Dm: 'D2 A3 D4 F4' };
  const ch = prog('F F Bb F F Dm C C Dm Bb F C Bb C F C'.split(' ').map((k) => V[k]).join(' | '));
  const mA = mel('C5:.5 F5:.5 A5:.5 F5:.5 C6 A5 | G5:.5 A5:.5 G5:.5 F5:.5 C5:2 | D5:.5 F5:.5 Bb5 A5:.5 G5:.5 F5 | ' +
                 'A5 G5:.5 F5:.5 C5:2 | C5:.5 F5:.5 A5:.5 C6:.5 D6 C6 | A5:.5 F5:.5 D5 F5 A5 | G5:1.5 F5:.5 E5 D5 | C5 E5 G5:2');
  const mB = mel('D6 C6:.5 A5:.5 F5 A5 | Bb5 A5:.5 G5:.5 F5 D5 | C5:.5 F5:.5 A5:.5 C6:.5 F6:2 | E6 D6:.5 C6:.5 G5:2 | ' +
                 'F5:.5 G5:.5 Bb5 D6 Bb5 | C6 Bb5:.5 A5:.5 G5:2 | A5:.5 G5:.5 F5:.5 G5:.5 A5 C6 | G5 E5 C5 r');
  for (let b = 0; b < 16; b++) {
    const c = ch[b], tb = t0 + b * bar;
    const fifth = c.root + 7 > 45 ? c.root - 5 : c.root + 7;
    // (levels: kick and bass land together on 1 and 3 and set the true peak; kept light so the fiddle and the
    // strums carry the loudness, or the whole loop has to be turned down)
    I.kick(bus, tb, 0.22); I.kick(bus, tb + 2 * spb, 0.22);
    I.pluck(bus, tb, c.root + 12, 0.5, { bright: 0.3, dur: 0.45, decay: 0.993 }); I.softBass(bus, tb, c.root, spb * 0.9, 0.55);
    I.pluck(bus, tb + 2 * spb, fifth + 12, 0.5, { bright: 0.3, dur: 0.45, decay: 0.993 }); I.softBass(bus, tb + 2 * spb, fifth, spb * 0.9, 0.55);
    for (const k of [1, 3]) {
      if (b === 15 && k === 3) continue;
      c.tones.forEach((m, j) => I.pluck(bus, tb + k * spb + j * 0.007, m, 0.5, { bright: 0.6, dur: 0.18, decay: 0.985 }));
      I.snare(bus, tb + k * spb, 4300 + b * 2 + k, 0.45);
    }
    for (let e = 0; e < 8; e++) I.hat(bus, tb + e * spb / 2, 4400 + b * 8 + e, false, e % 2 ? 0.9 : 0.5);
    if (b === 15) fillSnare(bus, tb + 3 * spb, s16, 4, 4600, 0.6);
  }
  play(mA, t0, spb, 4, 0, (t, m, s) => I.fiddle(bus, t, m, s * 0.9, 2));
  play(mB, t0, spb, 4, 8, (t, m, s) => I.fiddle(bus, t, m, s * 0.9, 2));
  return 16 * bar;
}

// 5 MOONLIT GARDEN (a summer night): E minor, 66 BPM, 8 bars. A music box with echoes, crickets, fireflies.
function moonBuild(bus, t0) {
  const bpm = 66, spb = 60 / bpm, bar = 4 * spb;
  const ch = prog('E2 E3 B3 G4 | C2 E3 B3 G4 | A1 E3 A3 C4 | B1 D#3 A3 F#4 | E2 E3 B3 G4 | C2 E3 G3 C4 | A1 E3 G3 C4 | B1 D#3 F#3 B3');
  const m = mel('B4 E5 G5 F#5 | E5:2 B4:2 | C5 E5 A5:1.5 G5:.5 | F#5:2 D#5:2 | G5 B5 E6 D6 | C6:1.5 B5:.5 G5:2 | A5 G5 E5 C5 | D#5:2 F#5 B4');
  for (let b = 0; b < 8; b++) {
    const c = ch[b], tb = t0 + b * bar;
    c.tones.forEach((n) => I.padX(bus, tb, n, bar, { gain: 0.03, att: 1.2, rel: 1.2, lp: 700 }));
    I.softBass(bus, tb, c.root + 12, bar * 0.9);
    I.cricket(bus, tb + (b % 2 ? 1.25 : 3.75) * spb);
    if (b % 2) I.sparkle(bus, tb + 2.5 * spb, c.tones[2] + 24, 1);
  }
  play(m, t0, spb, 4, 0, (t, n) => {
    I.musicBox(bus, t, n, 1); I.musicBox(bus, t + 0.75 * spb, n, 0.33); I.musicBox(bus, t + 1.5 * spb, n, 0.12);
  });
  return 8 * bar;
}
// E minor, 118 BPM, 16 bars: a soft night pulse, echoing plucks and a gliding theremin tune.
function moonWave(bus, t0) {
  const bpm = 118, spb = 60 / bpm, bar = 4 * spb, s16 = spb / 4;
  const V = { Em: 'E2 E4 G4 B4', C: 'C2 C4 E4 G4', Am: 'A1 A3 C4 E4', B: 'B1 B3 D#4 F#4' };
  const ch = prog('Em Em C C Am Am B B'.split(' ').map((k) => V[k]).join(' | '));
  const th = mel('E5:2 G5:2 | B5:3 A5 | G5:2 E5:2 | C6:4 | A5:2 C6:2 | E6:2 D6 C6 | B5:2 A5 F#5 | D#5:2 F#5:2');
  const arp = [0, 1, 2, 1, 0, 2, 1, 2, 0, 1, 2, 1, 0, 2, 1, 0];
  for (let b = 0; b < 16; b++) {
    const c = ch[b % 8], tb = t0 + b * bar;
    I.kick(bus, tb, 0.35); I.kick(bus, tb + 2.5 * spb, 0.3);
    I.rim(bus, tb + spb, 5100 + b * 2, 0.8); I.rim(bus, tb + 3 * spb, 5101 + b * 2, 0.8);
    for (let e = 0; e < 8; e++) I.hat(bus, tb + e * spb / 2, 5200 + b * 8 + e, false, e % 2 ? 0.7 : 0.35);
    for (let e = 0; e < 8; e++) I.subBass(bus, tb + e * spb / 2, c.root, spb / 2 * 0.8, e % 2 ? 0.55 : 0.8);
    for (let e = 0; e < 16; e++) {
      const n = c.tones[arp[e]] + 12, t = tb + e * s16;
      I.pluck(bus, t, n, 0.3, { bright: 0.5, dur: 0.3, decay: 0.993, wet: 0.6 });
      if (e % 2 === 0) I.pluck(bus, t + 3 * s16, n, 0.12, { bright: 0.4, dur: 0.3, decay: 0.993, wet: 0.8 });
    }
    if (b % 2 === 0) c.tones.forEach((n) => I.padX(bus, tb, n, 2 * bar, { gain: 0.022, att: 0.9, lp: 800 }));
    if (b >= 4 && b < 8) { I.musicBox(bus, tb, c.tones[2] + 12, 0.5); I.musicBox(bus, tb + 2 * spb, c.tones[1] + 12, 0.45); }
    if (b === 15) fillSnare(bus, tb + 3 * spb, s16, 4, 5400, 0.5);
  }
  play(th, t0, spb, 4, 8, (t, n, s, from) => I.theremin(bus, t, n, s * 0.95, from));
  return 16 * bar;
}

// 6 AMBER WOODS (autumn): B minor, 78 BPM, 10 bars. A nylon guitar, a low cello, falling-leaf bells, flute.
function amberBuild(bus, t0) {
  const bpm = 78, spb = 60 / bpm, bar = 4 * spb;
  const ch = prog('B1 F#3 B3 D4 | G1 D3 G3 B3 | D2 F#3 A3 D4 | A1 E3 A3 C#4 | B1 F#3 B3 D4 | E2 G3 B3 E4 | G1 D3 G3 B3 | ' +
                  'A1 E3 A3 C#4 | F#1 F#3 A3 C#4 | F#1 F#3 A#3 C#4');
  const fl = mel('F#5:1.5 E5:.5 D5 A4 | C#5:2 E5:2 | D5 F#5 B5:1.5 A5:.5 | G5:2 E5 B4 | D5:1.5 E5:.5 G5 F#5 | E5:3 r | ' +
                 'C#5 A4 F#4 A4 | A#4:2 C#5:2');
  const pat = [0, 1, 2, 3, 2, 1, 2, 3];
  for (let b = 0; b < 10; b++) {
    const c = ch[b], tb = t0 + b * bar, arr = [c.root + 12, c.tones[0], c.tones[1], c.tones[2]];
    for (let e = 0; e < 8; e++) I.pluck(bus, tb + e * spb / 2, arr[pat[e]], e === 0 ? 0.6 : 0.42, { bright: 0.3, dur: 1.0, decay: 0.997 });
    I.padX(bus, tb, c.root + 12, bar, { gain: 0.06, att: 0.4, rel: 0.5, lp: 600 });
    I.softBass(bus, tb, c.root + 12, 2 * spb, 0.7);
  }
  [[0, 0, 90], [0, 1.5, 86], [0, 3, 83], [1, 0, 86], [1, 1.5, 83], [1, 3, 79]]
    .forEach(([b, at, n]) => I.bell(bus, t0 + b * bar + at * spb, n, 1, spb, 0.5));   // leaves falling
  play(fl, t0, spb, 4, 2, (t, n, s) => I.flute(bus, t, n, s * 0.95));
  return 10 * bar;
}
// B minor, 136 BPM, 16 bars: a gallop through the woods, horn then saw lead.
function amberWave(bus, t0) {
  const bpm = 136, spb = 60 / bpm, bar = 4 * spb, s16 = spb / 4;
  const V = { Bm: 'B1 B3 D4 F#4', G: 'G1 G3 B3 D4', D: 'D2 A3 D4 F#4', A: 'A1 A3 C#4 E4', Em: 'E2 G3 B3 E4', 'F#': 'F#1 F#3 A#3 C#4' };
  const ch = prog('Bm G D A Bm G Em F#'.split(' ').map((k) => V[k]).join(' | '));
  const hA = mel('B4:2 D5 F#5 | G5:3 F#5 | F#5:2 E5 D5 | E5:4 | B4 D5 F#5 B5 | A5:2 G5 D5 | E5:2 G5 B5 | A#5:2 F#5:2');
  const lB = mel('F#5:.5 B5:.5 D6 C#6:.5 B5:.5 F#5 | G5:.5 B5:.5 D6 E6 D6 | F#6:1.5 E6:.5 D6 A5 | C#6 E6 A5:2 | ' +
                 'B5:.5 C#6:.5 D6 F#6 D6 | E6 D6:.5 B5:.5 G5:2 | G5 B5 E6 G6 | F#6 E6 C#6 A#5');
  for (let b = 0; b < 16; b++) {
    const c = ch[b % 8], tb = t0 + b * bar;
    I.kick(bus, tb); I.kick(bus, tb + 2 * spb);
    I.snare(bus, tb + spb, 6100 + b * 2, 0.85); if (b % 8 !== 7) I.snare(bus, tb + 3 * spb, 6101 + b * 2, 0.85);
    for (let e = 0; e < 8; e++) I.hat(bus, tb + e * spb / 2, 6200 + b * 8 + e, false, e % 2 ? 0.8 : 0.5);
    if (b % 8 === 7) [55, 52, 47, 43].forEach((n, e) => I.tom(bus, tb + 3 * spb + e * s16, n, 0.28));
    for (let k = 0; k < 4; k++) {   // gallop: dum-da-da on every beat
      const t = tb + k * spb;
      I.pumpBass(bus, t, c.root + 12, spb * 0.45); I.pumpBass(bus, t + 2 * s16, c.root + 12, s16 * 0.9);
      I.pumpBass(bus, t + 3 * s16, c.root + 12, s16 * 0.9);
    }
    I.subBass(bus, tb, c.root, bar * 0.95, 0.5);
    for (const k of [0, 2.5]) [24, 31, 36].forEach((d) => I.pluck(bus, tb + k * spb, c.root + d, 0.35, { bright: 0.45, dur: 0.35, decay: 0.99 }));
    if (b >= 8) I.horn(bus, tb, c.tones[1] + 12, bar * 0.95, 0.55);
  }
  play(hA, t0, spb, 4, 0, (t, m, s) => I.horn(bus, t, m, s * 0.95, 1.1));
  play(lB, t0, spb, 4, 8, (t, m, s) => I.lead(bus, t, m, s * 0.9));
  return 16 * bar;
}

// 7 PUMPKIN PATCH (autumn, crossroads; playful and a bit spooky): C minor, 100 BPM, 12 bars. A walking
// bassoon, pizzicato and a tiptoeing celesta.
function pumpkinBuild(bus, t0) {
  const bpm = 100, spb = 60 / bpm, bar = 4 * spb;
  const walk = ['C3 Eb3 G3 Eb3', 'C3 G2 Ab2 B2', 'F2 Ab2 C3 Ab2', 'G2 B2 D3 B2', 'C3 Eb3 G3 Eb3', 'Ab2 C3 Eb3 C3',
                'F2 Ab2 C3 Ab2', 'G2 B2 D3 B2', 'C3 Eb3 G3 Eb3', 'Ab2 C3 Eb3 C3', 'D3 F3 Ab3 F3', 'G2 B2 D3 F3'].map((s) => s.split(' ').map(nm));
  const pz = ['Eb4 G4', 'Eb4 G4', 'F4 Ab4', 'D4 G4', 'Eb4 G4', 'Eb4 Ab4', 'F4 Ab4', 'D4 G4', 'Eb4 G4', 'Eb4 Ab4', 'F4 Ab4', 'D4 F4']
    .map((s) => s.split(' ').map(nm));
  const cel = mel('G5:.5 r:.5 G5:.5 r:.5 Ab5:.5 G5:.5 Eb5 | C5:.5 r:.5 D5:.5 Eb5:.5 D5 r | Ab5:.5 r:.5 Ab5:.5 r:.5 C6:.5 Ab5:.5 F5 | ' +
                  'B4:.5 r:.5 D5:.5 F5:.5 G5 r | Eb6:.5 D6:.5 C6:.5 B5:.5 C6 G5 | Ab5:.5 G5:.5 F5:.5 Eb5:.5 C5:2 | ' +
                  'F5:.5 G5:.5 Ab5:.5 B5:.5 C6 Ab5 | G5 F#5:.5 G5:.5 B5 r | G5:.5 r:.5 C6:.5 r:.5 Eb6 D6 | C6:1.5 Ab5:.5 Eb5:2 | ' +
                  'F5:.5 Ab5:.5 D6:.5 C6:.5 Ab5 F5 | D5:.5 F5:.5 B5:.5 D6:.5 G5:2');
  for (let b = 0; b < 12; b++) {
    const tb = t0 + b * bar;
    walk[b].forEach((n, k) => I.bassoon(bus, tb + k * spb, n, spb * 0.55));
    for (const k of [0.5, 1.5, 2.5, 3.5]) pz[b].forEach((n) => I.pluck(bus, tb + k * spb, n, 0.35, { bright: 0.5, dur: 0.15, decay: 0.985 }));
    I.block(bus, tb + spb, 79, 0.7); I.block(bus, tb + 3 * spb, 84, 0.6);
    pz[b].forEach((n) => I.padX(bus, tb, n - 12, bar, { gain: 0.025, lp: 600, att: 0.5 }));
  }
  play(cel, t0, spb, 4, 0, (t, n) => I.musicBox(bus, t, n, 0.9));
  return 12 * bar;
}
// C minor, 148 BPM, 16 bars: two lines calling and answering, like the two paths crossing.
function pumpkinWave(bus, t0) {
  const bpm = 148, spb = 60 / bpm, bar = 4 * spb, s16 = spb / 4;
  const V = { Cm: 'C2 G3 C4 Eb4', Ab: 'Ab1 Ab3 C4 Eb4', G: 'G1 G3 B3 D4', Fm: 'F1 F3 Ab3 C4', Bb: 'Bb1 F3 Bb3 D4', Gm: 'G1 G3 Bb3 D4' };
  const ch = prog('Cm Cm Ab G Cm Cm Fm G Ab Bb Gm Cm Ab Fm G G'.split(' ').map((k) => V[k]).join(' | '));
  const call = mel('C5:.5 Eb5:.5 G5 r:2 | Ab5:.5 G5:.5 Eb5 r:2 | C6:.5 Ab5:.5 Eb5 r:2 | D5:.5 F5:.5 B5 r:2 | G5:.5 C6:.5 Eb6 r:2 | ' +
                   'D6:.5 C6:.5 G5 r:2 | F5:.5 Ab5:.5 C6 r:2 | B5 D6 r:2 | Ab5:.5 C6:.5 Eb6 r:2 | Bb5:.5 D6:.5 F6 r:2 | ' +
                   'G5:.5 Bb5:.5 D6 r:2 | C6:.5 Eb6:.5 G6 r:2 | Ab5:.5 G5:.5 F5:.5 Eb5:.5 r:2 | F5:.5 Eb5:.5 D5:.5 C5:.5 r:2 | ' +
                   'B4:.5 D5:.5 F5:.5 G5:.5 r:2 | G5:2 r:2');
  const resp = mel('r:2 G4:.5 Eb4:.5 C4 | r:2 Eb4:.5 F4:.5 G4 | r:2 Ab4:.5 G4:.5 Eb4 | r:2 D4:.5 B3:.5 G3 | r:2 C5:.5 G4:.5 Eb4 | ' +
                   'r:2 Eb4:.5 D4:.5 C4 | r:2 Ab4:.5 F4:.5 C4 | r:2 G4:.5 F4:.5 D4 | r:2 C5:.5 Ab4:.5 Eb4 | r:2 D5:.5 Bb4:.5 F4 | ' +
                   'r:2 D5:.5 Bb4:.5 G4 | r:2 Eb5:.5 C5:.5 G4 | r:2 C4:.5 Eb4:.5 Ab4 | r:2 Ab3:.5 C4:.5 F4 | r:2 D4:.5 F4:.5 B4 | ' +
                   'r:2 G4:.5 B4:.5 D5');
  const bp = [0, 0, 12, 0, 7, 0, 12, 7];
  for (let b = 0; b < 16; b++) {
    const c = ch[b], tb = t0 + b * bar;
    I.kick(bus, tb); I.kick(bus, tb + 2 * spb);
    I.snare(bus, tb + spb, 7100 + b * 2, 0.8); if (b !== 15) I.snare(bus, tb + 3 * spb, 7101 + b * 2, 0.8);
    for (let e = 0; e < 8; e++) I.hat(bus, tb + e * spb / 2, 7200 + b * 8 + e, false, e % 2 ? 0.8 : 0.5);
    if (b === 15) [60, 55, 60, 55, 52, 48, 45, 43].forEach((n, e) => { if (e >= 4) I.tom(bus, tb + 2 * spb + e * s16, n, 0.3); });
    if (b === 15) fillSnare(bus, tb + 2 * spb, s16, 4, 7400, 0.6);
    for (let e = 0; e < 8; e++) I.bassoon(bus, tb + e * spb / 2, c.root + 12 + bp[e], spb / 2 * 0.7, 0.9);
    c.tones.forEach((n) => I.padX(bus, tb, n, bar, { gain: 0.02, lp: 900, att: 0.3, rel: 0.4 }));
  }
  play(call, t0, spb, 4, 0, (t, m, s) => I.sqLead(bus, t, m, s * 0.85));
  play(resp, t0, spb, 4, 0, (t, m, s) => I.lead(bus, t, m, s * 0.85, 0.9));
  return 16 * bar;
}

// 8 FROST FEN (winter arrives): F# minor, 68 BPM, 8 bars. Glassy bells over a cold drone, ice ticking.
function frostBuild(bus, t0) {
  const bpm = 68, spb = 60 / bpm, bar = 4 * spb;
  const ch = prog('F#1 F#3 A3 C#4 | D2 F#3 A3 D4 | A1 E3 A3 C#4 | E2 E3 G#3 B3 | F#1 F#3 A3 C#4 | B1 F#3 B3 D4 | D2 F#3 A3 D4 | C#2 E#3 G#3 C#4');
  const m = mel('C#6:2 A5:2 | F#5:3 r | E5 A5 C#6:2 | B5:4 | A5 C#6 F#6:2 | D6:2 B5:2 | A5:1.5 F#5:.5 D5:2 | E#5:2 G#5:2');
  const ice = [[0.75, 3], [2.25, 1], [3.5, 2], [1.25, 0], [2.75, 3], [0.5, 2], [3.25, 1], [1.75, 3]];
  for (let b = 0; b < 8; b++) {
    const c = ch[b], tb = t0 + b * bar;
    c.tones.forEach((n) => { I.pad(bus, tb, n + 12, bar); I.padX(bus, tb, n + 24, bar, { wave: 'tri', gain: 0.012, lp: 3000, att: 1.0 }); });
    I.subBass(bus, tb, c.root + 12, bar * 0.98, 0.5);
    for (const [at, k] of [ice[b], ice[(b + 3) % 8]]) I.sparkle(bus, tb + at * spb, (c.tones[k % 3] || c.tones[0]) + 36, 0.9);
  }
  play(m, t0, spb, 4, 0, (t, n, s) => I.bell(bus, t, n, s / spb, spb, 1));
  return 8 * bar;
}
// F# minor, 128 BPM, 16 bars: crisp and cold, an icy pluck arpeggio, claps and a bell lead.
function frostWave(bus, t0) {
  const bpm = 128, spb = 60 / bpm, bar = 4 * spb, s16 = spb / 4;
  const V = { 'F#m': 'F#1 F#3 A3 C#4', D: 'D2 F#3 A3 D4', A: 'A1 E3 A3 C#4', E: 'E2 E3 G#3 B3', 'C#': 'C#2 E#3 G#3 C#4' };
  const ch = prog('F#m F#m D D A A E C#'.split(' ').map((k) => V[k]).join(' | '));
  const lead = mel('F#5 A5 C#6:1.5 B5:.5 | A5:2 F#5:2 | F#5 A5 D6:1.5 C#6:.5 | A5:4 | C#6 E6 A5 C#6 | B5:2 A5 E5 | G#5 B5 E6:2 | E#6:2 G#5 C#6');
  const ap = [0, 1, 2, 3, 2, 1, 2, 3, 0, 2, 1, 3, 2, 1, 0, 1];
  for (let b = 0; b < 16; b++) {
    const c = ch[b % 8], tb = t0 + b * bar, arr = [c.tones[0] + 12, c.tones[1] + 12, c.tones[2] + 12, c.tones[0] + 24];
    I.kick(bus, tb); I.kick(bus, tb + 2 * spb); if (b % 2) I.kick(bus, tb + 2.75 * spb, 0.3);
    I.clap(bus, tb + spb, 8100 + b * 8, 0.9); if (b !== 15) I.clap(bus, tb + 3 * spb, 8104 + b * 8, 0.9);
    for (let e = 0; e < 16; e++) I.hat(bus, tb + e * s16, 8300 + b * 16 + e, false, e % 4 === 2 ? 0.9 : 0.45);
    if (b === 15) fillSnare(bus, tb + 3 * spb, s16, 4, 8600, 0.7);
    for (let e = 0; e < 8; e++) I.subBass(bus, tb + e * spb / 2, c.root + 12, spb / 2 * 0.85, e % 2 ? 0.6 : 0.85);
    for (let e = 0; e < 16; e++) I.pluck(bus, tb + e * s16, arr[ap[e]], e % 4 ? 0.25 : 0.35, { bright: 0.9, dur: 0.18, decay: 0.99, wet: 0.5 });
    if (b >= 4 && b < 8) for (const k of [0, 2.5]) c.tones.forEach((n) => I.padX(bus, tb + k * spb, n + 12, 0.35, { gain: 0.03, att: 0.01, rel: 0.25, lp: 2500 }));
  }
  play(lead, t0, spb, 4, 8, (t, m, s) => { I.bell(bus, t, m, s / spb, spb, 1); I.sqLead(bus, t, m - 12, s * 0.9, 0.5); });
  return 16 * bar;
}

// 9 SNOWDRIFT HOLLOW (cozy deep winter): Eb major, 64 BPM, 8 bars. Sleigh bells, celesta, a warm kalimba.
function snowBuild(bus, t0) {
  const bpm = 64, spb = 60 / bpm, bar = 4 * spb;
  const ch = prog('Eb2 G3 Bb3 Eb4 | C2 G3 C4 Eb4 | Ab1 Ab3 C4 Eb4 | Bb1 F3 Bb3 D4 | Eb2 G3 Bb3 Eb4 | G1 G3 Bb3 D4 | Ab1 Ab3 C4 Eb4 | Bb1 F3 Ab3 D4');
  const m = mel('G5:1.5 F5:.5 Eb5 Bb4 | C5 Eb5 G5:2 | Ab5 G5 F5 Eb5 | F5:3 r | Bb5:1.5 Ab5:.5 G5 Eb5 | D5 G5 Bb5:2 | C6 Bb5 Ab5 F5 | D5:2 F5 Bb4');
  const pat = [0, 1, 2, 1, 0, 2, 1, 2];
  for (let b = 0; b < 8; b++) {
    const c = ch[b], tb = t0 + b * bar;
    c.tones.forEach((n) => I.pad(bus, tb, n, bar));
    I.softBass(bus, tb, c.root + 12, 2 * spb); I.softBass(bus, tb + 2 * spb, c.root + 19 > 52 ? c.root + 7 : c.root + 19, 1.8 * spb, 0.8);
    for (let e = 0; e < 8; e++) I.kalimba(bus, tb + e * spb / 2, c.tones[pat[e]] + 12, e % 2 ? 0.45 : 0.6);
    I.sleigh(bus, tb + spb, 9100 + b * 2, 0.6); I.sleigh(bus, tb + 3 * spb, 9101 + b * 2, 0.6);
  }
  play(m, t0, spb, 4, 0, (t, n) => I.musicBox(bus, t, n, 1));
  return 8 * bar;
}
// G minor, 140 BPM, 16 bars: sledding downhill, sleigh bells on every eighth.
function snowWave(bus, t0) {
  const bpm = 140, spb = 60 / bpm, bar = 4 * spb, s16 = spb / 4;
  const V = { Gm: 'G1 G3 Bb3 D4', Eb: 'Eb2 G3 Bb3 Eb4', Bb: 'Bb1 F3 Bb3 D4', F: 'F2 F3 A3 C4', Cm: 'C2 G3 C4 Eb4', D: 'D2 F#3 A3 D4' };
  const ch = prog('Gm Eb Bb F Gm Eb Cm D'.split(' ').map((k) => V[k]).join(' | '));
  const mA = mel('D5:.5 G5:.5 Bb5:.5 G5:.5 D6 Bb5 | C6:.5 Bb5:.5 G5 Eb5:2 | F5:.5 Bb5:.5 D6:.5 Bb5:.5 F6 D6 | C6:2 A5:2 | ' +
                 'G5:.5 A5:.5 Bb5:.5 C6:.5 D6 G6 | F6 Eb6:.5 D6:.5 Bb5:2 | C6 Eb6 G5 C6 | F#5 A5 D6:2');
  const mB = mel('G5:1.5 Bb5:.5 D6:2 | Eb6 D6 Bb5:2 | D6:1.5 C6:.5 Bb5 F5 | A5:3 r | Bb5 A5 G5 D5 | G5:1.5 F5:.5 Eb5:2 | ' +
                 'Eb5 G5 C6 Eb6 | D6 C6 A5 F#5');
  const bassOn = [1, 0, 1, 1, 0, 1, 0, 1];
  for (let b = 0; b < 16; b++) {
    const c = ch[b % 8], tb = t0 + b * bar;
    I.kick(bus, tb); I.kick(bus, tb + 2 * spb);
    I.snare(bus, tb + spb, 9300 + b * 2, 0.8); if (b !== 15) I.snare(bus, tb + 3 * spb, 9301 + b * 2, 0.8);
    for (let e = 0; e < 8; e++) I.sleigh(bus, tb + e * spb / 2, 9400 + b * 8 + e, e % 2 ? 0.55 : 0.9);
    if (b === 15) fillSnare(bus, tb + 3 * spb, s16, 4, 9700, 0.7);
    for (let e = 0; e < 8; e++) if (bassOn[e]) I.pumpBass(bus, tb + e * spb / 2, c.root + 12 + (e % 2 ? 12 : 0), spb / 2 * 0.85);
    I.subBass(bus, tb, c.root, 2 * spb * 0.9, 0.45); I.subBass(bus, tb + 2 * spb, c.root, 2 * spb * 0.9, 0.45);
    if (b < 8) for (let e = 0; e < 4; e++) I.kalimba(bus, tb + (e + 0.5) * spb, c.tones[(e + b) % 3] + 12, 0.45);
    else c.tones.forEach((n) => I.padX(bus, tb, n + 12, bar, { gain: 0.02, lp: 1200, att: 0.4, rel: 0.5 }));
  }
  play(mA, t0, spb, 4, 0, (t, m) => I.musicBox(bus, t, m, 0.95));
  play(mB, t0, spb, 4, 8, (t, m, s) => I.lead(bus, t, m, s * 0.9));
  return 16 * bar;
}

// 10 EVERGREEN SUMMIT (the finale, on the snowy summit): C major, 80 BPM, 10 bars. Home key: Sunny
// Meadow's opening bell phrase comes back on a horn, over the meadow's kalimba, with timpani.
function summitBuild(bus, t0) {
  const bpm = 80, spb = 60 / bpm, bar = 4 * spb, s16 = spb / 4;
  const ch = prog('C2 E3 G3 C4 | A1 E3 A3 C4 | F1 F3 A3 C4 | G1 D3 G3 B3 | C2 E3 G3 C4 | E2 E3 G3 B3 | F1 F3 A3 C4 | ' +
                  'D2 D3 F3 A3 | F1 F3 Ab3 C4 | G1 D3 G3 B3');
  const hm = mel('E5:1.5 D5:.5 G5:2 | E5 C5 A4:2 | C5:1.5 D5:.5 E5 A5 | G5:4 | G5:1.5 A5:.5 G5 E5 | B5:2 G5:2 | ' +
                 'A5 C6 F5:2 | D5 F5 A5:2 | Ab5:2 G5 F5 | G5:2 D5 B4');
  const arpA = [0, 2, 3, 1, 2, 3, 1, 2];
  for (let b = 0; b < 10; b++) {
    const c = ch[b], tb = t0 + b * bar, up = [c.tones[0] + 12, c.tones[1] + 12, c.tones[2] + 12, c.tones[1] + 24];
    c.tones.forEach((n) => { I.pad(bus, tb, n + 12, bar); I.padX(bus, tb, n + 24, bar, { gain: 0.014, lp: 1500 }); });
    I.softBass(bus, tb, c.root + 12, 2 * spb); I.softBass(bus, tb + 2 * spb, c.root + 12, 1.8 * spb, 0.8);
    for (let e = 0; e < 8; e++) I.kalimba(bus, tb + e * spb / 2, up[arpA[e]], e % 2 ? 0.5 : 0.7);
    if (b % 4 === 0) I.timpani(bus, tb, c.root + 12, 0.4);
    if (b === 9) for (let e = 8; e < 16; e++) I.timpani(bus, tb + e * s16, 43, 0.12 + 0.2 * (e - 8) / 8);
  }
  play(hm, t0, spb, 4, 0, (t, n, s) => { I.horn(bus, t, n, s * 0.95, 1.2); if (t - t0 >= 4 * bar) I.bell(bus, t, n + 12, s / spb, spb, 0.4); });
  return 10 * bar;
}
// C minor into the major, 152 BPM, 20 bars: everything at once, the biggest track in the game.
function summitWave(bus, t0) {
  const bpm = 152, spb = 60 / bpm, bar = 4 * spb, s16 = spb / 4;
  const V = { Cm: 'C2 G3 C4 Eb4', Ab: 'Ab1 Ab3 C4 Eb4', Bb: 'Bb1 F3 Bb3 D4', G: 'G1 G3 B3 D4', Eb: 'Eb2 G3 Bb3 Eb4',
              Fm: 'F1 F3 Ab3 C4', Gm: 'G1 G3 Bb3 D4' };
  const ch = prog('Cm Ab Bb G Cm Ab Eb Bb Cm Ab Fm G Ab Bb Gm Cm Ab Bb G G'.split(' ').map((k) => V[k]).join(' | '));
  const lA = mel('G5 C6 Eb6:1.5 D6:.5 | C6:2 Ab5:2 | Bb5 G5 Eb6 G6 | F6:3 D6 | Eb6 D6:.5 C6:.5 G5 C6 | Ab5 C6 Eb6:2 | ' +
                 'F6 Eb6 C6 Ab5 | B5:2 D6:2');
  const lB = mel('C6:1.5 Bb5:.5 Eb6:2 | D6 Bb5 F5:2 | G5:1.5 A5:.5 Bb5 D6 | C6:2 G6:2 | Ab6 G6 Eb6 C6 | D6 F6 Bb6:2 | B6:2 G6:2 | ' +
                 'D6 B5 G5 F5');
  const ap = [0, 1, 2, 1, 0, 2, 1, 2, 0, 1, 2, 3, 2, 1, 2, 1];
  for (let b = 0; b < 20; b++) {
    const c = ch[b], tb = t0 + b * bar, big = b >= 12;
    I.kick(bus, tb); I.kick(bus, tb + 2 * spb); if (big) I.kick(bus, tb + 2.5 * spb, 0.35);
    I.snare(bus, tb + spb, 10100 + b * 2, 0.9); if (b !== 3 && b !== 19) I.snare(bus, tb + 3 * spb, 10101 + b * 2, 0.9);
    for (let e = 0; e < 8; e++) I.hat(bus, tb + e * spb / 2, 10200 + b * 8 + e, big && e % 2 === 1, e % 2 ? 0.8 : 0.5);
    if (b === 3 || b === 19) [57, 55, 52, 48].forEach((n, e) => I.tom(bus, tb + 3 * spb + e * s16, n, 0.3));
    if (b === 0 || b === 12) I.crash(bus, tb, 10500 + b, 1);
    if (b === 0 || b === 4 || b === 12) I.timpani(bus, tb, c.root + 12, 0.45);
    for (let e = 0; e < 8; e++) I.pumpBass(bus, tb + e * spb / 2, c.root + 12 + (e % 2 ? 12 : 0), spb / 2 * 0.9);
    I.subBass(bus, tb, c.root, bar * 0.95, 0.4);
    const up = [c.tones[1] + 12, c.tones[2] + 12, c.tones[0] + 24, c.tones[1] + 24];
    for (let e = 0; e < 16; e++) I.arp(bus, tb + e * s16, up[ap[e]]);
    if (b < 4) { for (const k of [0, 1.5, 3]) c.tones.forEach((n) => I.horn(bus, tb + k * spb, n + 12, spb * 0.9, 0.45)); }
    if (big) { I.horn(bus, tb, c.tones[2] + 12, 2 * spb * 0.95, 0.6); I.horn(bus, tb + 2 * spb, c.tones[1] + 12, 2 * spb * 0.95, 0.6); }
    c.tones.forEach((n) => I.padX(bus, tb, n + 12, bar, { gain: 0.016, lp: 1400, att: 0.3, rel: 0.5 }));
  }
  play(lA, t0, spb, 4, 4, (t, m, s) => I.lead(bus, t, m, s * 0.9));
  play(lB, t0, spb, 4, 12, (t, m, s) => { I.lead(bus, t, m, s * 0.9, 1.05); I.horn(bus, t, m - 12, s * 0.9, 0.5); });
  return 20 * bar;
}

// map number (1-based, as the game shows it) -> its loops. Map 1 is the original build/wave pair.
const MAP_LOOPS = [
  { map: 2, title: 'Apple Orchard', build: [orchardBuild, 100, 'G major', 3], wave: [orchardWave, 140, 'G major'] },
  { map: 3, title: 'Thorny Ridge', build: [ridgeBuild, 70, 'D dorian'], wave: [ridgeWave, 144, 'D minor'] },
  { map: 4, title: 'Harvest Hollow', build: [harvestBuild, 96, 'F major'], wave: [harvestWave, 136, 'F major'] },
  { map: 5, title: 'Moonlit Garden', build: [moonBuild, 66, 'E minor'], wave: [moonWave, 118, 'E minor'] },
  { map: 6, title: 'Amber Woods', build: [amberBuild, 78, 'B minor'], wave: [amberWave, 136, 'B minor'] },
  { map: 7, title: 'Pumpkin Patch', build: [pumpkinBuild, 100, 'C minor'], wave: [pumpkinWave, 148, 'C minor'] },
  { map: 8, title: 'Frost Fen', build: [frostBuild, 68, 'F# minor'], wave: [frostWave, 128, 'F# minor'] },
  { map: 9, title: 'Snowdrift Hollow', build: [snowBuild, 64, 'Eb major'], wave: [snowWave, 140, 'G minor'] },
  { map: 10, title: 'Evergreen Summit', build: [summitBuild, 80, 'C major'], wave: [summitWave, 152, 'C minor'] },
];

// ------------------------------------------------------------------ render
function renderLoop(fn) {
  // measure L with a dry run on a tiny bus
  const L = fn({ dry: new Float64Array(1), wet: new Float64Array(1) }, 0);
  const bus = newBus(3 * L + 4);
  // Each cycle starts on a WHOLE sample (bus.off), so every note lands on the same sample in every
  // cycle and the result is exactly periodic. The loop length is that whole number of samples.
  const Ls = Math.round(L * RATE);
  for (let k = 0; k < 3; k++) { bus.off = k * Ls; fn(bus, 0); }
  bus.off = 0;
  const full = mixdown(bus, 1);
  const a = Ls - Math.round(PRE * RATE), n = Ls + Math.round((PRE + POST) * RATE);
  return { samples: full.slice(a, a + n), loopStart: PRE, loopEnd: PRE + Ls / RATE, length: Ls / RATE };
}
function renderOnce(fn) {
  const len = fn({ dry: new Float64Array(1), wet: new Float64Array(1) }, 0);
  const bus = newBus(len + 2.5);
  fn(bus, 0.02);
  const out = mixdown(bus, 1);
  // trim to len + reverb tail, with a 60 ms fade so it ends in silence
  const n = Math.round((len + 0.6) * RATE), fade = Math.round(0.06 * RATE);
  const s = out.slice(0, n);
  for (let i = 0; i < fade; i++) s[n - 1 - i] *= i / fade;
  return { samples: s, length: n / RATE };
}

function writeWavF32(file, samples) {
  const data = Buffer.alloc(samples.length * 4);
  for (let i = 0; i < samples.length; i++) data.writeFloatLE(samples[i], i * 4);
  const h = Buffer.alloc(44);
  h.write('RIFF', 0); h.writeUInt32LE(36 + data.length, 4); h.write('WAVE', 8);
  h.write('fmt ', 12); h.writeUInt32LE(16, 16); h.writeUInt16LE(3, 20); h.writeUInt16LE(1, 22);
  h.writeUInt32LE(RATE, 24); h.writeUInt32LE(RATE * 4, 28); h.writeUInt16LE(4, 32); h.writeUInt16LE(32, 34);
  h.write('data', 36); h.writeUInt32LE(data.length, 40);
  fs.writeFileSync(file, Buffer.concat([h, data]));
}

function main() {
  const i = process.argv.indexOf('--out');
  const out = i > 0 ? process.argv[i + 1] : path.join(__dirname, 'out');
  fs.mkdirSync(out, { recursive: true });
  const layout = {};
  const tracks = [
    ['build', () => renderLoop(buildLoop), { bpm: 88, key: 'C major', map: 1 }],
    ['wave', () => renderLoop(waveLoop), { bpm: 132, key: 'A minor', map: 1 }],
    ['win', () => renderOnce(winStinger), { key: 'C major' }],
    ['lose', () => renderOnce(loseStinger), { key: 'A minor' }],
  ];
  for (const m of MAP_LOOPS)
    for (const phase of ['build', 'wave']) {
      const [fn, bpm, key, meter] = m[phase];
      tracks.push([`${phase}-m${m.map}`, () => renderLoop(fn), Object.assign({ bpm, key, map: m.map }, meter ? { meter: `${meter}/4` } : {})]);
    }
  const only = process.argv.indexOf('--only');   // --only name[,name...]: render just these (quick listening)
  const pick = only > 0 ? new Set(process.argv[only + 1].split(',')) : null;
  for (const [name, render, meta] of tracks) {
    if (pick && !pick.has(name)) continue;
    const r = render();
    let peak = 0;
    for (const v of r.samples) peak = Math.max(peak, Math.abs(v));
    writeWavF32(path.join(out, name + '.wav'), r.samples);
    layout[name] = Object.assign({ seconds: +(r.samples.length / RATE).toFixed(4), peak: +peak.toFixed(4) }, meta,
      r.loopEnd ? { loop: true, loopStart: r.loopStart, loopEnd: +r.loopEnd.toFixed(6), loopLength: +r.length.toFixed(6) }
                : { loop: false });
    console.log(name, JSON.stringify(layout[name]));
  }
  fs.writeFileSync(path.join(out, 'layout.json'), JSON.stringify(layout, null, 1) + '\n');
}

if (require.main === module) main();
module.exports = { RATE, mtof, MAP_LOOPS, I, renderLoop };   // I, renderLoop: for mix experiments
