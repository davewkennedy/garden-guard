#!/usr/bin/env node
// Garden Guard web: checks for the music module (owner: Bobby). Serves web/ over http (fetch() can't read file://),
// drives headless Chromium over CDP with real mouse/keyboard events, and checks:
//   no sound or download before the first gesture; the build loop starts on the first gesture with the loop window from
//   audio/music.json; the wave loop crossfades in on a phase change; stingers duck the loop (and a held one stays ducked
//   until release()); volume and on/off are saved, separate from the sound effects, and survive a reload; the context is
//   suspended while the tab is hidden; the music panel, the M key; the 7 header icons fit on small phones; sw.js lists
//   the audio files for offline use; no console errors.
//   node tools/music-check.js          (works with the placeholder files or Dale's real ones)
'use strict';
const { ensureWebSocket, sleep, openPage } = require('./cdp');
ensureWebSocket(__filename);
const http = require('http'), fs = require('fs'), path = require('path');

const ROOT = path.resolve(__dirname, '..');
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.json': 'application/json', '.png': 'image/png', '.mp3': 'audio/mpeg', '.svg': 'image/svg+xml' };
const server = http.createServer((q, r) => {
  let p = decodeURIComponent(new URL(q.url, 'http://x').pathname); if (p === '/') p = '/index.html';
  const f = path.join(ROOT, p);
  if (!f.startsWith(ROOT) || !fs.existsSync(f) || fs.statSync(f).isDirectory()) { r.writeHead(404); return r.end('404'); }
  r.writeHead(200, { 'Content-Type': MIME[path.extname(f)] || 'application/octet-stream' }); fs.createReadStream(f).pipe(r);
});

let failures = 0;
const t0 = Date.now(), stamp = () => ((Date.now() - t0) / 1000).toFixed(1).padStart(5) + 's';
function check(ok, what, extra) { console.log(`  ${ok ? '[ ok ]' : '[FAIL]'} ${what}${ok || extra === undefined ? '' : '   ' + JSON.stringify(extra)}`); if (!ok) failures++; return ok; }
function step(s) { console.log(`${stamp()} ${s}`); }

async function click(cdp, x, y) {
  await cdp.send('Input.dispatchMouseEvent', { type: 'mouseMoved', x, y });
  await cdp.send('Input.dispatchMouseEvent', { type: 'mousePressed', x, y, button: 'left', clickCount: 1 });
  await cdp.send('Input.dispatchMouseEvent', { type: 'mouseReleased', x, y, button: 'left', clickCount: 1 });
}
async function key(cdp, k, code) {
  await cdp.send('Input.dispatchKeyEvent', { type: 'keyDown', key: k, code, text: k.length === 1 ? k : undefined, windowsVirtualKeyCode: k.length === 1 ? k.toUpperCase().charCodeAt(0) : 27 });
  await cdp.send('Input.dispatchKeyEvent', { type: 'keyUp', key: k, code });
}
async function waitFor(cdp, expr, ms = 6000) { const t = Date.now(); for (;;) { try { if (await cdp.eval(expr)) return true; } catch (e) { /* */ } if (Date.now() - t > ms) return false; await sleep(80); } }
const audioFetches = `performance.getEntriesByType('resource').filter((e) => /\\/audio\\//.test(e.name)).map((e) => e.name.split('/').pop())`;

(async () => {
  await new Promise((r) => server.listen(0, '127.0.0.1', r));
  const base = 'http://127.0.0.1:' + server.address().port;
  const hasAudio = fs.existsSync(path.join(ROOT, 'audio', 'music.json'));
  check(hasAudio, 'web/audio/music.json exists (placeholders: node tools/make-music-placeholders.js; or Dale\'s real files)');
  if (!hasAudio) { server.close(); process.exit(1); }

  // ---------- before the first gesture ----------
  step('first visit: nothing before a gesture');
  let { cdp, errors, close } = await openPage(base + '/index.html');
  const ev = (e) => cdp.eval(e);
  await ev("localStorage.clear()");
  ({ cdp, errors, close } = await (async () => { close(); return openPage(base + '/index.html'); })());
  const ev2 = (e) => cdp.eval(e);
  let st = await ev2('Music.state()');
  check(st.ctx === null && st.playing === null, 'no audio context and nothing playing before the first gesture', st);
  check((await ev2(audioFetches)).length === 0, 'no music file is downloaded before the first gesture');
  check(st.enabled === true && Math.abs(st.volume - 0.6) < 1e-9, 'default: music ON at volume 0.6', st);

  step('first gesture: the build loop starts');
  await click(cdp, 60, 300);
  check(await waitFor(cdp, "Music.state().ctx === 'running' && Music.state().playing === 'build'"), 'the first click starts the build loop', await ev2('Music.state()'));
  st = await ev2('Music.state()');
  const meta = await ev2("fetch('audio/music.json').then((r) => r.json())");
  const mb = meta.build || {};
  const expA = mb.loopStart >= 0 && mb.loopEnd > mb.loopStart ? mb.loopStart : (mb.loop > 0 ? 0.5 : 0);
  check(st.loopWindow && Math.abs(st.loopWindow.a - expA) < 1e-6 && st.loopWindow.z > st.loopWindow.a, 'the loop window comes from music.json (starts at loopStart)', { loopWindow: st.loopWindow, meta: mb });
  check(st.loaded.includes('build'), 'the build loop was decoded once', st.loaded);
  await sleep(1500);
  check((await ev2('Music.state()')).loaded.includes('wave'), 'the wave loop is lazy-loaded after the build loop');
  check(Math.abs(st.master - 0.06) < 0.002, 'master gain is 0.1 x volume (0.6 -> 0.06, -24 dB)', st.master);

  // ---------- phase: build -> wave -> build ----------
  step('phase changes crossfade the loops');
  await ev2("UI.startRunAt(0); UI.closeCard && ['ovIntro','ovMapSelect'].forEach((i) => UI.closeCard(i)); 1");
  await sleep(300);
  await ev2("GG.game.coins = 999; GG.game.startWave(); 1");
  check(await waitFor(cdp, "GG.game.phase === 'Wave'"), 'a wave starts (phase Wave)');
  check(await waitFor(cdp, "Music.state().playing === 'wave'", 4000), 'the music moves to the wave loop', await ev2('Music.state()'));
  await ev2("GG.game.phase = 'Build'; 1");   // test hack: end the wave; UI.sync follows the game phase
  check(await waitFor(cdp, "Music.state().playing === 'build'", 4000), 'back to the build loop when the phase is Build again');

  // ---------- stingers duck the loop ----------
  step('stingers');
  await ev2("Music.stinger('win', false); 1");
  await sleep(500);
  const d1 = (await ev2('Music.state()')).duck;
  check(d1 < 0.6, 'a win stinger ducks the loop', d1);
  const until = (await ev2('Music.state()')).stingerUntil;
  const back = await waitFor(cdp, 'Music.state().duck > 0.9', 12000);   // stinger length + the 1.5 s fade back up
  const dl = await ev2('Music.state()');
  check(back || until === 0, 'after the stinger the loop fades back up (not held)', dl);
  await ev2("Music.stinger('lose', true); 1");
  await sleep(4500);
  let held = await ev2('Music.state()');
  check(held.held && held.duck < 0.35, 'a held (Lost/Won) stinger keeps the loop ducked', held);
  await ev2("Music.release(); 1");
  await sleep(2200);
  held = await ev2('Music.state()');
  check(!held.held && held.duck > 0.9 && held.playing === 'build', 'release() (a new run) brings the build loop back up', held);

  // ---------- maps: every map picks its own loops (Dale) ----------
  step('maps: each map plays its own build and wave loop');
  const maps = meta.maps || [];
  check(maps.length === 10, 'music.json lists 10 maps', maps.length);
  const played = new Set();
  let maxLoaded = 0;
  for (let i = 0; i < 10; i++) {
    const m = maps[i] || {};
    const names = await ev2(`[Music.loopName(${i}, 'build'), Music.loopName(${i}, 'wave')]`);
    check(names[0] === m.build && names[1] === m.wave && meta[m.build] && meta[m.wave],
      `map ${i + 1} (${m.title}): the player's loops ${names.join('/')} are music.json's`, { names, m });
    await ev2(`UI.startRunAt(${i}); ['ovIntro','ovMapSelect','ovBugs'].forEach((id) => UI.closeCard(id)); 1`);
    const b = await waitFor(cdp, `Music.state().playing === ${JSON.stringify(m.build)}`, 8000);
    if (b) played.add(m.build);
    await ev2("GG.game.coins = 999; GG.game.startWave(); 1");
    const w = await waitFor(cdp, `Music.state().playing === ${JSON.stringify(m.wave)}`, 8000);
    if (w) played.add(m.wave);
    check(b && w, `map ${i + 1} (${m.title}): build plays ${m.build}, a wave plays ${m.wave}`, await ev2('Music.state()'));
    await ev2("GG.game.phase = 'Build'; 1");
    await waitFor(cdp, `Music.state().playing === ${JSON.stringify(m.build)}`, 4000);
    await sleep(1900);   // the crossfade ends and the previous map's buffers are dropped
    maxLoaded = Math.max(maxLoaded, (await ev2('Music.state()')).loaded.length);
  }
  check(played.size === 20, `every map picks its own track: ${played.size} different loops played (want 20)`, [...played]);
  check(maxLoaded <= 6, `only the current map's loops, the menu loop and the stingers stay decoded (at most ${maxLoaded} buffers)`);
  await ev2("UI.openCard('ovMapSelect'); 1");
  check(await waitFor(cdp, "Music.state().playing === 'build'", 4000), 'the map list (a menu) plays Sunny Meadow\'s build loop', await ev2('Music.state()'));
  await ev2("UI.closeCard('ovMapSelect'); 1");
  check(await waitFor(cdp, `Music.state().playing === ${JSON.stringify(maps[9] && maps[9].build)}`, 4000),
    'closing it crossfades back to the map\'s own loop', await ev2('Music.state()'));
  await ev2("UI.startRunAt(0); ['ovIntro','ovMapSelect','ovBugs'].forEach((id) => UI.closeCard(id)); 1");
  check(await waitFor(cdp, "Music.state().playing === 'build'", 4000), 'back on Sunny Meadow: its build loop');

  // ---------- settings: separate from the sound effects, saved ----------
  step('settings');
  await ev2("Sfx.muted = true; 1");
  check((await ev2('Music.enabled()')) === true, 'muting the sound effects does not stop the music');
  await ev2("Sfx.muted = false; Music.setVolume(0.3); 1");
  await sleep(300);
  st = await ev2('Music.state()');
  check(Math.abs(st.master - 0.03) < 0.002 && (await ev2("localStorage.getItem('ggweb:musicVolume')")) === '0.3', 'volume 0.3 -> gain 0.03, saved', st.master);
  await ev2("Music.setEnabled(false); 1");
  await sleep(600);
  st = await ev2('Music.state()');
  check(st.playing === null && (await ev2("localStorage.getItem('ggweb:music')")) === '0', 'turning music off stops it and saves 0', st);
  check((await ev2('Sfx.enabled()')) === true, 'and leaves the sound effects on');
  step('reload with music off: nothing starts');
  const loadedAgain = new Promise((r) => cdp.on((m) => m === 'Page.loadEventFired' && r()));
  await cdp.send('Page.reload', { ignoreCache: true });
  await Promise.race([loadedAgain, sleep(8000)]); await sleep(500);
  const ev3 = (e) => cdp.eval(e);
  await click(cdp, 60, 300); await sleep(800);
  st = await ev3('Music.state()');
  check(!st.enabled && Math.abs(st.volume - 0.3) < 1e-9 && st.ctx === null && (await ev3(audioFetches)).length === 0, 'music off and volume 0.3 survive a reload; no context, no download', st);

  // ---------- UI: panel and M key ----------
  step('panel and keyboard');
  await ev3("UI.closeCard('ovIntro'); 1");
  const bb = await ev3("(() => { const r = document.getElementById('btnSound').getBoundingClientRect(); return { x: r.x + r.width / 2, y: r.y + r.height / 2 }; })()");
  await click(cdp, bb.x, bb.y); await sleep(200);
  check(await ev3("!document.getElementById('soundPanel').classList.contains('hidden')"), 'the speaker button opens the sound panel');
  check(await ev3("document.getElementById('chkMusic').checked === false && document.getElementById('rngMusic').disabled === true && document.getElementById('rngMusic').value === '30'"), 'the panel shows music off and volume 30');
  await ev3("(() => { const c = document.getElementById('chkMusic'); c.checked = true; c.dispatchEvent(new Event('change', { bubbles: true })); })()");
  check(await waitFor(cdp, "Music.state().ctx === 'running' && Music.state().playing === 'build'"), 'ticking Music starts the loop (inside that gesture)', await ev3('Music.state()'));
  await ev3("(() => { const r = document.getElementById('rngMusic'); r.value = '80'; r.dispatchEvent(new Event('input', { bubbles: true })); })()");
  await sleep(200);
  check(Math.abs((await ev3('Music.state()')).volume - 0.8) < 1e-9, 'the slider sets the volume');
  await ev3("(() => { const c = document.getElementById('chkSfx'); c.checked = false; c.dispatchEvent(new Event('change', { bubbles: true })); })()");
  check((await ev3('Sfx.muted')) === true && (await ev3('Music.enabled()')) === true, 'the panel\'s Sound effects switch mutes only the effects');
  await ev3("(() => { const c = document.getElementById('chkSfx'); c.checked = true; c.dispatchEvent(new Event('change', { bubbles: true })); })()");
  await key(cdp, 'Escape', 'Escape'); await sleep(150);
  check(await ev3("document.getElementById('soundPanel').classList.contains('hidden')"), 'Esc closes the panel');
  await ev3("UI.closeCard('ovIntro'); 1");
  await key(cdp, 'm', 'KeyM'); await sleep(500);
  check((await ev3('Music.enabled()')) === false, 'the M key turns music off', await ev3('Music.state()'));
  await key(cdp, 'm', 'KeyM'); await sleep(300);
  check((await ev3('Music.enabled()')) === true, 'and on again');

  // ---------- the tab is hidden ----------
  step('tab hidden');
  await ev3("Object.defineProperty(document, 'hidden', { configurable: true, get: () => true }); document.dispatchEvent(new Event('visibilitychange')); 1");
  await sleep(300);
  check((await ev3('Music.state()')).ctx === 'suspended', 'the music is suspended while the tab is hidden');
  await ev3("Object.defineProperty(document, 'hidden', { configurable: true, get: () => false }); document.dispatchEvent(new Event('visibilitychange')); 1");
  check(await waitFor(cdp, "Music.state().ctx === 'running'", 3000), 'and resumes when it is shown again');
  check(errors.length === 0, 'no console errors on this page', errors);

  // ---------- a fade cut short must not jump (Dale's finding) ----------
  step('fade cut short');
  const reloaded2 = new Promise((r) => cdp.on((m) => m === 'Page.loadEventFired' && r()));
  await cdp.send('Page.reload', { ignoreCache: true });
  await Promise.race([reloaded2, sleep(8000)]); await sleep(500);
  await ev3("localStorage.setItem('ggweb:music', '1'); 1");
  await ev3("Music.setEnabled(true); 1");
  await click(cdp, 60, 300);
  await waitFor(cdp, "Music.state().playing === 'build'");
  await sleep(500);                                   // the first fade-in (1.5 s) is part-way
  const g0 = (await ev3('Music.state()')).curGain;
  await ev3("Music.setEnabled(false); 1");            // turn off in the middle of that fade-in
  const g1 = (await ev3('Music.state()')).lastFadeFrom;
  check(g0 > 0 && g0 < 0.9 && g1 !== null && Math.abs(g1 - g0) < 0.25 && g1 < 0.9, 'turning music off during the fade-in fades out from the CURRENT gain, not from full level', { g0, g1 });
  close();

  // ---------- small phones: the 7 header icons fit ----------
  for (const size of ['se', 'iphone', 'android']) {
    step('header icons on ' + size);
    const r = await openPage(base + '/index.html', size);
    await r.cdp.eval("document.querySelectorAll('.overlay').forEach((o) => o.classList.add('hidden')); 1");
    const g = await r.cdp.eval(`(() => { const bs = [...document.querySelectorAll('#header .icons button')].filter((b) => b.offsetParent);
      const rects = bs.map((b) => b.getBoundingClientRect()); const col = document.getElementById('header').getBoundingClientRect();
      const overlap = rects.some((a, i) => rects.some((b, j) => j > i && a.left < b.right - 0.5 && b.left < a.right - 0.5 && a.top < b.bottom - 0.5 && b.top < a.bottom - 0.5));
      return { n: bs.length, maxRight: Math.max(...rects.map((x) => x.right)), colRight: col.right, w: Math.min(...rects.map((x) => x.width)), overlap }; })()`);
    check(g.n >= 6 && g.w >= 31.9 && !g.overlap && g.maxRight <= g.colRight + 1, `${size}: ${g.n} icons fit without overlap, each at least 32px (narrowest ${Math.round(g.w)}px)`, g);
    check(r.errors.length === 0, `${size}: no console errors`, r.errors);
    r.close();
  }

  // ---------- offline list ----------
  step('offline');
  const sw = fs.readFileSync(path.join(ROOT, 'sw.js'), 'utf8');
  const files = fs.readdirSync(path.join(ROOT, 'audio')).filter((n) => /\.(mp3|json)$/i.test(n));
  const missing = files.filter((n) => !sw.includes('"audio/' + n + '"'));
  check(files.length >= 5 && missing.length === 0, 'sw.js lists every audio file for offline play (run tools/make-sw-list.sh)', missing);

  server.close();
  console.log(failures ? `\nMUSIC CHECK FAILED (${failures})` : '\nMUSIC CHECK PASSED');
  process.exit(failures ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });
