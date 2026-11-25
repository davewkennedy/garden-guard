// Garden Guard++ offline cache (owner: Bill). Cache-first for every game file, so once the game
// has been opened over https it also plays with no signal.
// FILES and CACHE are generated: run `bash tools/make-sw-list.sh` after changing the game. It lists every
// file index.html loads and sets CACHE to a content hash, so phones pick up each new version.
const CACHE = 'ggweb-0cd6eb5bcb';
const FILES = [
  "./",
  "index.html",
  "manifest.json",
  "icon-180.png",
  "icon-192.png",
  "css/style.css",
  "css/phone.css",
  "css/portrait.css",
  "js/data.js",
  "js/game.js",
  "js/audio.js",
  "js/music.js",
  "js/view.js",
  "js/renderer.js",
  "js/autopilot.js",
  "js/journal.js",
  "js/difficulty.js",
  "js/focusfire.js",
  "js/ui.js",
  "js/input.js",
  "js/scenes.js",
  "js/main.js",
  "icon-512.png",
  "audio/music-build-m10.mp3",
  "audio/music-build-m2.mp3",
  "audio/music-build-m3.mp3",
  "audio/music-build-m4.mp3",
  "audio/music-build-m5.mp3",
  "audio/music-build-m6.mp3",
  "audio/music-build-m7.mp3",
  "audio/music-build-m8.mp3",
  "audio/music-build-m9.mp3",
  "audio/music-build.mp3",
  "audio/music-wave-m10.mp3",
  "audio/music-wave-m2.mp3",
  "audio/music-wave-m3.mp3",
  "audio/music-wave-m4.mp3",
  "audio/music-wave-m5.mp3",
  "audio/music-wave-m6.mp3",
  "audio/music-wave-m7.mp3",
  "audio/music-wave-m8.mp3",
  "audio/music-wave-m9.mp3",
  "audio/music-wave.mp3",
  "audio/music.json",
  "audio/stinger-lose.mp3",
  "audio/stinger-win.mp3",
];

self.addEventListener('install', event => {
  // One file at a time, so a missing file doesn't stop the rest from being cached.
  event.waitUntil(caches.open(CACHE)
    .then(c => Promise.all(FILES.map(f => c.add(new Request(f, { cache: 'reload' })).catch(() => {}))))
    .then(() => self.skipWaiting()));
});

self.addEventListener('activate', event => {
  event.waitUntil(caches.keys()
    .then(keys => Promise.all(keys.filter(k => k.startsWith('ggweb-') && k !== CACHE).map(k => caches.delete(k))))
    .then(() => self.clients.claim()));
});

self.addEventListener('fetch', event => {
  const req = event.request;
  if (req.method !== 'GET' || new URL(req.url).origin !== self.location.origin) return;
  event.respondWith(
    caches.match(req, { ignoreSearch: true }).then(hit => hit || fetch(req).then(res => {
      if (res.ok) { const copy = res.clone(); caches.open(CACHE).then(c => c.put(req, copy)); }
      return res;
    }).catch(() => (req.mode === 'navigate' ? caches.match('index.html') : Response.error())))
  );
});
