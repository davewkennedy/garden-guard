#!/usr/bin/env node
// Garden Guard++ web: installability check (owner: Bill; CDP plumbing copied from Dale's browser-run.js).
// Serves the folder on http://127.0.0.1:<port> (python3 http.server), opens it in headless chromium and asks
// Chrome itself: Page.getAppManifest (parse errors), Page.getInstallabilityErrors, service worker control,
// manifest icons, offline reload, and whether the "📲 Install as an app" button (beforeinstallprompt) shows.
//   node tools/pwa-check.js [--no-bypass]   (WSL, any folder)   exit 0 = installable, 1 = problems
//   By default chromium skips its user-engagement heuristic so beforeinstallprompt can fire headless;
//   --no-bypass keeps the heuristic (like a first-time visitor).
'use strict';
if (typeof WebSocket === 'undefined') {
    const r = require('child_process').spawnSync(process.execPath,
        ['--experimental-websocket', '--no-warnings', __filename, ...process.argv.slice(2)], { stdio: 'inherit' });
    process.exit(r.status === null ? 1 : r.status);
}
const { spawn, execSync } = require('child_process');
const fs = require('fs'), os = require('os'), path = require('path');
const ROOT = path.join(__dirname, '..');
const PORT = 8000 + Math.floor(Math.random() * 900);
const sleep = ms => new Promise(r => setTimeout(r, ms));
let failures = 0;
const check = (ok, what) => { console.log(`  ${ok ? '[ ok ]' : '[FAIL]'} ${what}`); if (!ok) failures++; return ok; };

function launch() {
    const bin = ['chromium', 'chromium-browser', 'google-chrome'].map(b => {
        try { return execSync('command -v ' + b, { encoding: 'utf8' }).trim(); } catch (e) { return ''; }
    }).find(Boolean);
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'ggweb-pwa-'));
    const proc = spawn(bin, ['--headless=new', '--remote-debugging-port=0', '--user-data-dir=' + dir, '--no-sandbox',
        '--disable-gpu', '--mute-audio', '--no-first-run', '--window-size=1280,800',
        ...(process.argv.includes('--no-bypass') ? [] : ['--bypass-app-banner-engagement-checks']),
        'about:blank'], { stdio: ['ignore', 'ignore', 'pipe'] });
    return new Promise((resolve, reject) => {
        let buf = '';
        proc.stderr.on('data', d => { buf += d; const m = buf.match(/DevTools listening on (ws:\/\/[^\s]+)/);
            if (m) resolve({ proc, dir, port: new URL(m[1]).port }); });
        proc.on('exit', c => reject(new Error('chromium exited ' + c)));
    });
}
class Cdp {
    constructor(url) {
        this.id = 0; this.pending = new Map(); this.ws = new WebSocket(url);
        this.ready = new Promise((res, rej) => { this.ws.onopen = res; this.ws.onerror = rej; });
        this.ws.onmessage = ev => { const m = JSON.parse(ev.data);
            if (m.id && this.pending.has(m.id)) { const p = this.pending.get(m.id); this.pending.delete(m.id);
                m.error ? p.rej(new Error(p.method + ': ' + m.error.message)) : p.res(m.result); } };
    }
    send(method, params = {}) { const id = ++this.id; this.ws.send(JSON.stringify({ id, method, params }));
        return new Promise((res, rej) => this.pending.set(id, { res, rej, method })); }
    async eval(expr) { const r = await this.send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true });
        if (r.exceptionDetails) throw new Error(r.exceptionDetails.text); return r.result.value; }
}

(async () => {
    let server = spawn('python3', ['-m', 'http.server', String(PORT), '--bind', '127.0.0.1'], { cwd: ROOT, stdio: 'ignore' });
    await sleep(800);
    const { proc, dir, port } = await launch();
    const cleanup = () => { try { proc.kill(); } catch (e) {} try { server && server.kill(); } catch (e) {}
        try { fs.rmSync(dir, { recursive: true, force: true }); } catch (e) {} };
    try {
        const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
        const cdp = new Cdp(targets.find(t => t.type === 'page').webSocketDebuggerUrl);
        await cdp.ready;
        await cdp.send('Page.enable');
        await cdp.send('Runtime.enable');
        const base = `http://127.0.0.1:${PORT}/`;
        console.log(`serving ${ROOT} at ${base}`);
        await cdp.send('Page.navigate', { url: base + 'index.html' });
        await sleep(2500);
        // Second load so the freshly installed worker controls the page (clients.claim also does this).
        await cdp.send('Page.reload', {});
        await sleep(2500);

        const man = await cdp.send('Page.getAppManifest');
        check(!!man.url && man.url.endsWith('manifest.json'), `manifest linked: ${man.url}`);
        check(!man.errors || man.errors.length === 0, 'manifest parses with no errors' +
            (man.errors && man.errors.length ? ': ' + man.errors.map(e => e.message).join('; ') : ''));
        let parsed = {};
        try { parsed = JSON.parse(man.data || '{}'); } catch (e) {}
        const p = man.parsed || {};
        console.log(`        name="${parsed.name}" short_name="${parsed.short_name}" display=${parsed.display} ` +
            `orientation=${parsed.orientation} start_url=${p.startUrl || parsed.start_url} scope=${p.scope || parsed.scope}`);

        const inst = await cdp.send('Page.getInstallabilityErrors');
        const errs = inst.installabilityErrors || [];
        check(errs.length === 0, 'Chrome: installable' + (errs.length ? ' -> ' + errs.map(e => e.errorId +
            (e.errorArguments || []).map(a => ` ${a.name}=${a.value}`).join('')).join(', ') : ''));

        try {
            const icons = await cdp.send('Page.getManifestIcons');
            check(!!(icons && icons.primaryIcon), 'Chrome picked a primary manifest icon');
        } catch (e) { console.log('        (Page.getManifestIcons not available: ' + e.message + ')'); }

        const sw = await cdp.eval(`navigator.serviceWorker.getRegistration().then(r => ({
            reg: !!r, active: !!(r && r.active), scope: r && r.scope, controlled: !!navigator.serviceWorker.controller }))`);
        check(sw.reg && sw.active, `service worker registered + active (scope ${sw.scope})`);
        check(sw.controlled, 'service worker controls the page');
        const cached = await cdp.eval(`caches.keys().then(async ks => { const out = {}; for (const k of ks)
            out[k] = (await (await caches.open(k)).keys()).length; return out; })`);
        console.log('        caches: ' + JSON.stringify(cached));

        const btn = await cdp.eval(`(() => { const b = document.getElementById('btnInstall');
            return b ? { exists: true, shown: !b.classList.contains('hidden') && !!b.offsetParent } : { exists: false }; })()`);
        if (btn.exists) console.log(`  [info] 📲 Install button ${btn.shown ? 'SHOWN (beforeinstallprompt fired)' :
            'hidden (headless chromium may never fire beforeinstallprompt; not counted as a failure)'}`);

        // Offline: stop the server and reload; the page must come from the cache and boot.
        server.kill(); server = null;
        await sleep(500);
        await cdp.send('Page.reload', { ignoreCache: false });
        await sleep(2500);
        const off = await cdp.eval(`({ title: document.title, game: typeof UI !== 'undefined' && !!UI.game,
            shop: document.querySelectorAll('.shopItem').length })`);
        check(off.game && off.shop > 0, `offline reload boots the game (title "${off.title}", ${off.shop} shop items)`);
    } catch (e) {
        console.log('ERROR ' + e.message); failures++;
    } finally { cleanup(); }
    console.log(failures ? `PWA CHECK: ${failures} problem(s)` : 'PWA CHECK OK: installable and works offline');
    process.exit(failures ? 1 : 0);
})();
