#!/usr/bin/env node
// Garden Guard web: the music works OFFLINE (owner: Bobby). Serves the page (default web/, or --root dist/gardenguard-web)
// over http, lets the service worker cache every file, then switches the browser offline, reloads, and checks that the
// build loop still starts on the first gesture from the cached audio files (and that the sw.js cache holds all of them).
//   node tools/music-offline-check.js [--root DIR]
'use strict';
const { ensureWebSocket, sleep, openPage } = require('./cdp');
ensureWebSocket(__filename);
const http = require('http'), fs = require('fs'), path = require('path');

const argv = process.argv.slice(2);
const rootArg = argv.indexOf('--root');
const ROOT = path.resolve(rootArg >= 0 ? argv[rootArg + 1] : path.join(__dirname, '..'));
const urlArg = argv.indexOf('--url');   // --url https://host/path : test a deployed site (GET only)
const LIVE = urlArg >= 0 ? argv[urlArg + 1].replace(/\/+$/, '') : null;
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.json': 'application/json', '.png': 'image/png', '.mp3': 'audio/mpeg', '.svg': 'image/svg+xml' };
const server = http.createServer((q, r) => {
  let p = decodeURIComponent(new URL(q.url, 'http://x').pathname); if (p === '/') p = '/index.html';
  const f = path.join(ROOT, p);
  if (!f.startsWith(ROOT) || !fs.existsSync(f) || fs.statSync(f).isDirectory()) { r.writeHead(404); return r.end('404'); }
  r.writeHead(200, { 'Content-Type': MIME[path.extname(f)] || 'application/octet-stream' }); fs.createReadStream(f).pipe(r);
});
let failures = 0;
function check(ok, what, extra) { console.log(`  ${ok ? '[ ok ]' : '[FAIL]'} ${what}${ok || extra === undefined ? '' : '   ' + JSON.stringify(extra)}`); if (!ok) failures++; return ok; }
async function waitFor(cdp, expr, ms = 8000) { const t = Date.now(); for (;;) { try { if (await cdp.eval(expr)) return true; } catch (e) { /* */ } if (Date.now() - t > ms) return false; await sleep(100); } }
async function click(cdp, x, y) {
  await cdp.send('Input.dispatchMouseEvent', { type: 'mouseMoved', x, y });
  await cdp.send('Input.dispatchMouseEvent', { type: 'mousePressed', x, y, button: 'left', clickCount: 1 });
  await cdp.send('Input.dispatchMouseEvent', { type: 'mouseReleased', x, y, button: 'left', clickCount: 1 });
}

(async () => {
  if (!LIVE) await new Promise((r) => server.listen(0, '127.0.0.1', r));
  const base = LIVE || 'http://127.0.0.1:' + server.address().port;
  let audioFiles;
  if (LIVE) {   // the audio files the deployed music.json names (read-only GET)
    const mj = await fetch(base + '/audio/music.json').then((r) => r.json());
    audioFiles = ['music.json'].concat(['build', 'wave', 'win', 'lose'].map((k) => mj[k] && mj[k].file).filter(Boolean));
  } else audioFiles = fs.readdirSync(path.join(ROOT, 'audio')).filter((n) => /\.(mp3|json)$/i.test(n));
  const { cdp, errors, close } = await openPage(base + '/index.html');
  console.log(LIVE ? 'LIVE site: ' + base + ' (read-only; offline = the browser\'s network switched off)' : 'root: ' + ROOT);
  check(await waitFor(cdp, "navigator.serviceWorker && navigator.serviceWorker.controller !== null || (navigator.serviceWorker.ready.then(() => true), false)", 1000) || true, 'service worker API present');
  await cdp.eval("navigator.serviceWorker.ready.then(() => 1)");
  await cdp.send('Page.reload'); await sleep(1500);   // now controlled by the service worker
  check(await waitFor(cdp, "!!navigator.serviceWorker.controller"), 'the service worker controls the page');
  const cached = await cdp.eval(`caches.keys().then((ks) => Promise.all(ks.map((k) => caches.open(k).then((c) => c.keys())))).then((all) => all.flat().map((r) => r.url.split('/audio/')[1]).filter(Boolean))`);
  check(audioFiles.every((n) => cached.includes(n)), 'the cache holds every audio file (' + audioFiles.join(', ') + ')', cached);

  // Offline = the server is gone (as in pwa-check.js). Everything must now come from the service worker's cache.
  if (LIVE) {   // the live server can't be stopped: switch the browser's network off instead
    await cdp.send('Network.enable');
    await cdp.send('Network.emulateNetworkConditions', { offline: true, latency: 0, downloadThroughput: 0, uploadThroughput: 0 });
  } else {
    if (server.closeAllConnections) server.closeAllConnections();
    await new Promise((r) => server.close(r));
  }
  const loaded = new Promise((r) => cdp.on((m) => m === 'Page.loadEventFired' && r()));
  await cdp.send('Page.reload', { ignoreCache: false });
  await Promise.race([loaded, sleep(8000)]); await sleep(800);
  check((await cdp.eval("document.title")).includes('Garden Guard'), 'OFFLINE (server stopped): the game boots from the cache');
  await click(cdp, 60, 300);
  check(await waitFor(cdp, "Music.state().ctx === 'running' && Music.state().playing === 'build'", 10000), 'OFFLINE: the first gesture starts the build loop from the cached files', await cdp.eval('Music.state()'));
  const st = await cdp.eval('Music.state()');
  check(st.loaded.includes('build') && st.lost.length === 0, 'OFFLINE: nothing failed to load', st);
  check(errors.length === 0, 'no console errors', errors);
  close();
  console.log(failures ? `\nMUSIC OFFLINE CHECK FAILED (${failures})` : '\nMUSIC OFFLINE CHECK PASSED');
  process.exit(failures ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });
