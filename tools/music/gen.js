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
  softBass: (b, t, m, secs) => voice(b, { t, dur: secs, f: mtof(m), wave: 'sine', gain: 0.34, att: 0.01,
                                          h2: 0.15, wet: 0.1 }),
  shaker: (b, t, seed, vel = 1) => voice(b, { t, dur: 0.06, wave: 'sine', noise: seed, hp: 6500, gain: 0.05 * vel,
                                              att: 0.004 }),
  kick: (b, t, g = 0.45) => { voice(b, { t, dur: 0.28, f: 150, f2: 46, glide: 0.12, wave: 'sine', gain: g, att: 0.002 });
                    voice(b, { t, dur: 0.02, wave: 'sine', noise: 777, lp: 2500, gain: 0.12, att: 0.001 }); },
  snare: (b, t, seed) => { voice(b, { t, dur: 0.17, wave: 'sine', noise: seed, bp: 1900, q: 0.8, gain: 0.32,
                                      att: 0.002, wet: 0.35 });
                           voice(b, { t, dur: 0.09, f: 210, f2: 165, wave: 'tri', gain: 0.18, att: 0.002 }); },
  hat: (b, t, seed, open) => voice(b, { t, dur: open ? 0.14 : 0.045, wave: 'sine', noise: seed, hp: 7500,
                                        gain: open ? 0.07 : 0.06, att: 0.002 }),
  pumpBass: (b, t, m, secs) => voice(b, { t, dur: secs, f: mtof(m), wave: 'square', gain: 0.11, att: 0.004,
                                          lp: 520, q: 1.1, rel: 0.03 }),
  arp: (b, t, m) => voice(b, { t, dur: 0.16, f: mtof(m), wave: 'square', gain: 0.05, att: 0.002, lp: 2600,
                               wet: 0.3 }),
  lead: (b, t, m, secs, vel = 1) => { voice(b, { t, dur: secs + 0.12, f: mtof(m), wave: 'saw', detune: 6,
                                                 gain: 0.075 * vel, att: 0.012, rel: 0.1, lp: 2300, vib: 0.004,
                                                 wet: 0.45 });
                                      voice(b, { t, dur: secs + 0.12, f: mtof(m), wave: 'tri', gain: 0.07 * vel,
                                                 att: 0.012, rel: 0.1, wet: 0.45 }); },
};

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
    ['build', () => renderLoop(buildLoop), { bpm: 88, key: 'C major' }],
    ['wave', () => renderLoop(waveLoop), { bpm: 132, key: 'A minor' }],
    ['win', () => renderOnce(winStinger), { key: 'C major' }],
    ['lose', () => renderOnce(loseStinger), { key: 'A minor' }],
  ];
  for (const [name, render, meta] of tracks) {
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
module.exports = { RATE, mtof };
