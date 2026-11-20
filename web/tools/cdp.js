// Garden Guard++ web: tiny Chrome DevTools Protocol helper for the test tools (owner: Dale).
// No npm: headless chromium + Node's built-in WebSocket (Node 20: --experimental-websocket;
// call ensureWebSocket() first and it re-execs the script with that flag).
'use strict';
const { spawn, execSync, spawnSync } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

function ensureWebSocket(file) {
    if (typeof WebSocket !== 'undefined') return;
    const r = spawnSync(process.execPath, ['--experimental-websocket', '--no-warnings', file, ...process.argv.slice(2)], { stdio: 'inherit' });
    process.exit(r.status === null ? 1 : r.status);
}

const SIZES = { pc: [1280, 800, false], iphone: [844, 390, true], android: [915, 412, true],
    'iphone-portrait': [390, 844, true], ipad: [1180, 820, true], 'ipad-portrait': [820, 1180, true], se: [667, 375, true] };
const sleep = (ms) => new Promise(r => setTimeout(r, ms));

function launch(W, H) {
    const bin = ['chromium', 'chromium-browser', 'google-chrome'].map(b => {
        try { return execSync('command -v ' + b, { encoding: 'utf8' }).trim(); } catch (e) { return ''; }
    }).find(Boolean);
    if (!bin) throw new Error('chromium not found');
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'ggweb-cdp-'));
    const proc = spawn(bin, ['--headless=new', '--remote-debugging-port=0', '--user-data-dir=' + dir,
        '--no-sandbox', '--disable-gpu', '--mute-audio', '--hide-scrollbars', '--no-first-run',
        '--allow-file-access-from-files', '--autoplay-policy=no-user-gesture-required',
        `--window-size=${W},${H}`, 'about:blank'], { stdio: ['ignore', 'ignore', 'pipe'] });
    return new Promise((resolve, reject) => {
        let buf = '';
        const timer = setTimeout(() => reject(new Error('chromium did not start')), 20000);
        proc.stderr.on('data', d => {
            buf += d;
            const m = buf.match(/DevTools listening on (ws:\/\/[^\s]+)/);
            if (m) { clearTimeout(timer); resolve({ proc, dir, port: new URL(m[1]).port }); }
        });
        proc.on('exit', c => reject(new Error('chromium exited ' + c + '\n' + buf.slice(-2000))));
    });
}

class Cdp {
    constructor(wsUrl) {
        this.id = 0; this.pending = new Map(); this.handlers = [];
        this.ws = new WebSocket(wsUrl);
        this.ready = new Promise((res, rej) => { this.ws.onopen = res; this.ws.onerror = rej; });
        this.ws.onmessage = (ev) => {
            const msg = JSON.parse(ev.data);
            if (msg.id && this.pending.has(msg.id)) {
                const { res, rej, method } = this.pending.get(msg.id);
                this.pending.delete(msg.id);
                msg.error ? rej(new Error(method + ': ' + msg.error.message)) : res(msg.result);
            } else if (msg.method) this.handlers.forEach(h => h(msg.method, msg.params));
        };
    }
    send(method, params = {}) {
        const id = ++this.id;
        this.ws.send(JSON.stringify({ id, method, params }));
        return new Promise((res, rej) => this.pending.set(id, { res, rej, method }));
    }
    on(fn) { this.handlers.push(fn); }
    async eval(expr) {
        const r = await this.send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true });
        if (r.exceptionDetails) throw new Error('eval: ' + (r.exceptionDetails.exception?.description || r.exceptionDetails.text) + '\n  in: ' + expr.slice(0, 200));
        return r.result.value;
    }
    async touch(type, points) {
        await this.send('Input.dispatchTouchEvent', { type, touchPoints: points.map((p, i) => ({ x: p.x, y: p.y, id: i, radiusX: 4, radiusY: 4, force: 1 })) });
    }
    async tap(p, holdMs = 40) {
        await this.touch('touchStart', [p]);
        await sleep(holdMs);
        await this.touch('touchEnd', []);
    }
}

// Opens a page with device emulation, collecting console errors / uncaught exceptions in `errors`.
async function openPage(url, size) {
    const [W, H, MOBILE] = SIZES[size] || SIZES.pc;
    const { proc, dir, port } = await launch(W, H);
    const list = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
    const cdp = new Cdp(list.find(t => t.type === 'page').webSocketDebuggerUrl);
    await cdp.ready;
    const errors = [];
    cdp.on((method, p) => {
        if (method === 'Runtime.exceptionThrown') {
            const d = p.exceptionDetails;
            errors.push(`uncaught: ${d.exception?.description?.split('\n').slice(0, 3).join(' | ') || d.text}`);
        } else if (method === 'Runtime.consoleAPICalled' && (p.type === 'error' || p.type === 'assert')) {
            const f = p.stackTrace?.callFrames?.[0];
            errors.push(`console.${p.type}: ${p.args.map(a => a.value ?? a.description ?? a.type).join(' ')}${f ? ` (${f.url.split('/').pop()}:${f.lineNumber + 1})` : ''}`);
        } else if (method === 'Log.entryAdded' && p.entry.level === 'error') {
            errors.push(`log: ${p.entry.text} ${p.entry.url ? '(' + p.entry.url.split('/').pop() + ')' : ''}`);
        }
    });
    await cdp.send('Runtime.enable');
    await cdp.send('Log.enable');
    await cdp.send('Page.enable');
    await cdp.send('Emulation.setDeviceMetricsOverride', { width: W, height: H, deviceScaleFactor: MOBILE ? 3 : 1, mobile: MOBILE,
        screenOrientation: H > W ? { type: 'portraitPrimary', angle: 0 } : { type: 'landscapePrimary', angle: MOBILE ? 90 : 0 } });
    if (MOBILE) await cdp.send('Emulation.setTouchEmulationEnabled', { enabled: true, maxTouchPoints: 5 });
    const loaded = new Promise(r => cdp.on(m => m === 'Page.loadEventFired' && r()));
    await cdp.send('Page.navigate', { url });
    await Promise.race([loaded, sleep(10000)]);
    await sleep(300);
    const close = () => {
        try { cdp.ws.close(); } catch (e) {}
        proc.kill('SIGKILL');
        setTimeout(() => { try { fs.rmSync(dir, { recursive: true, force: true }); } catch (e) {} }, 300);
    };
    return { cdp, errors, close, W, H };
}

module.exports = { ensureWebSocket, SIZES, sleep, launch, Cdp, openPage };
