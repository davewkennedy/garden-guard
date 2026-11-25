#!/usr/bin/env node
// Measures the game's synthesized SFX (web/js/audio.js, rendered exactly as the game renders them)
// so the music's default playback gain can be set relative to them (owner: Dale).
//   node tools/music/sfx-level.js   -> per-SFX sample peak and max momentary loudness, plus a summary
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const { spawnSync, execFileSync } = require('child_process');

const ROOT = path.join(__dirname, '..', '..');
const ctx = { window: {}, localStorage: { getItem: () => null, setItem() {} }, performance: { now: () => 0 },
              Math, Object, Float32Array };
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(path.join(ROOT, 'web', 'js', 'audio.js'), 'utf8') + '\nthis.Sfx = Sfx;', ctx);
const Sfx = ctx.Sfx, RATE = 44100;
const FF = process.env.FFMPEG || '/mnt/c/ProgramData/chocolatey/bin/ffmpeg.exe';
const out = path.join(__dirname, 'out');
fs.mkdirSync(out, { recursive: true });
const p = f => (FF.endsWith('.exe') ? execFileSync('wslpath', ['-w', f]).toString().trim() : f);

function wav(file, x) {
  const d = Buffer.alloc(x.length * 4);
  for (let i = 0; i < x.length; i++) d.writeFloatLE(x[i], i * 4);
  const h = Buffer.alloc(44);
  h.write('RIFF', 0); h.writeUInt32LE(36 + d.length, 4); h.write('WAVE', 8); h.write('fmt ', 12);
  h.writeUInt32LE(16, 16); h.writeUInt16LE(3, 20); h.writeUInt16LE(1, 22); h.writeUInt32LE(RATE, 24);
  h.writeUInt32LE(RATE * 4, 28); h.writeUInt16LE(4, 32); h.writeUInt16LE(32, 34); h.write('data', 36);
  h.writeUInt32LE(d.length, 40);
  fs.writeFileSync(file, Buffer.concat([h, d]));
}
const rows = [];
for (const name of Sfx.names()) {
  const x = Sfx._render(Sfx._recipes[name], RATE);
  // pad to 1 s so the 400 ms momentary meter sees the whole sound
  const y = new Float32Array(Math.max(x.length, RATE) + RATE / 2);
  y.set(x, RATE / 4);
  let peak = 0;
  for (const v of x) peak = Math.max(peak, Math.abs(v));
  const f = path.join(out, `sfx-${name}.wav`);
  wav(f, y);
  const s = spawnSync(FF, ['-hide_banner', '-nostats', '-v', 'verbose', '-i', p(f), '-af', 'ebur128=framelog=verbose', '-f', 'null', '-'])
    .stderr.toString();
  let mmax = -Infinity;
  for (const m of s.matchAll(/ M:\s*(-?[\d.]+)/g)) mmax = Math.max(mmax, parseFloat(m[1]));
  rows.push({ name, peakDb: 20 * Math.log10(peak), momentaryMax: mmax });
}
rows.sort((a, b) => b.momentaryMax - a.momentaryMax);
for (const r of rows) console.log(`${r.name.padEnd(16)} peak ${r.peakDb.toFixed(1).padStart(6)} dBFS   ` +
                                  `max momentary ${r.momentaryMax.toFixed(1).padStart(6)} LUFS`);
const med = a => a.slice().sort((p, q) => p - q)[Math.floor(a.length / 2)];
console.log(`median peak ${med(rows.map(r => r.peakDb)).toFixed(1)} dBFS, median momentary ` +
            `${med(rows.map(r => r.momentaryMax)).toFixed(1)} LUFS, loudest momentary ${rows[0].momentaryMax.toFixed(1)} LUFS`);
