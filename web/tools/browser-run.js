#!/usr/bin/env node
// Garden Guard++ web: real-browser test runner over the Chrome DevTools Protocol (owner: Dale).
// No npm: it launches headless chromium with --remote-debugging-port and talks CDP over Node's
// built-in WebSocket (Node 20 needs --experimental-websocket; this script re-execs itself with it).
//
//   node tools/browser-run.js [options]            (WSL, from the gardenguard++web folder)
//     --query "scene=play&map=3"   URL params for index.html
//     --size pc|iphone|android     viewport (1280x800 / 844x390 / 915x412); phones = mobile + touch
//     --seconds N                  run this long (real time) after load, collecting errors (default 3)
//     --touch                      touch QA: tap a shop item, tap a tile -> plant placed; tap/double-tap
//                                  the plant; long-press; 2-finger pinch -> View.zoom changes
//     --campaign                   autopilot campaign: waits until UI.game.phase is Won/Lost
//                                  (or --seconds runs out, default 600); prints progress per map
//     --shot out.png               screenshot at the end
//     --page path.html             another page instead of index.html (e.g. tools/stub.html)
//     --eval "expr"                evaluate a JS expression in the page at the end and print it
//   Exit code: 0 = no console errors / exceptions and all checks passed, 1 otherwise.
'use strict';
if (typeof WebSocket === 'undefined') {   // re-exec with the flag that enables Node 20's WebSocket
    const r = require('child_process').spawnSync(process.execPath,
        ['--experimental-websocket', '--no-warnings', __filename, ...process.argv.slice(2)], { stdio: 'inherit' });
    process.exit(r.status === null ? 1 : r.status);
}
const { spawn } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const ROOT = path.join(__dirname, '..');
const argv = process.argv.slice(2);
const opt = (name, dflt) => { const i = argv.indexOf('--' + name); return i >= 0 && i + 1 < argv.length ? argv[i + 1] : dflt; };
const flag = (name) => argv.includes('--' + name);
const SIZES = { pc: [1280, 800, false], iphone: [844, 390, true], android: [915, 412, true] };
const size = opt('size', 'pc');
const [W, H, MOBILE] = SIZES[size] || SIZES.pc;
const campaign = flag('campaign');
const seconds = Number(opt('seconds', campaign ? 600 : 3));
const page = path.resolve(ROOT, opt('page', 'index.html'));
const query = opt('query', campaign ? 'autopilot=1&speed=20' : '');
const url = 'file://' + page + (query ? '?' + query : '');
const sleep = (ms) => new Promise(r => setTimeout(r, ms));
const t0 = Date.now();
const stamp = () => ((Date.now() - t0) / 1000).toFixed(1).padStart(5) + 's';

let failures = 0;
function check(ok, what) { console.log(`  ${ok ? '[ ok ]' : '[FAIL]'} ${what}`); if (!ok) failures++; return ok; }

// ---------- chromium + CDP ----------
function launch() {
    const bin = ['chromium', 'chromium-browser', 'google-chrome'].map(b => {
        try { return require('child_process').execSync('command -v ' + b, { encoding: 'utf8' }).trim(); } catch (e) { return ''; }
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
        if (r.exceptionDetails) throw new Error('eval: ' + (r.exceptionDetails.exception?.description || r.exceptionDetails.text));
        return r.result.value;
    }
}

// ---------- page helpers ----------
const STATE = `(() => { try { const g = UI.game; return g ? { map: g.mapIndex + 1, wave: g.wave, lives: g.lives,
    coins: g.coins, phase: g.phase, plants: g.towers.length, bugs: g.enemies.length } : null; } catch (e) { return null; } })()`;
const fmt = s => s ? `map ${s.map} wave ${s.wave} lives ${s.lives} coins ${s.coins} phase ${s.phase} plants ${s.plants}` : '(no UI.game)';

async function touch(cdp, type, points) {
    await cdp.send('Input.dispatchTouchEvent', { type, touchPoints: points.map((p, i) => ({ x: p.x, y: p.y, id: i, radiusX: 4, radiusY: 4, force: 1 })) });
}
async function tap(cdp, p, holdMs = 40) {
    await touch(cdp, 'touchStart', [p]);
    await sleep(holdMs);
    await touch(cdp, 'touchEnd', []);
}

async function touchTests(cdp) {
    console.log(`${stamp()} touch QA (${size})`);
    // Close any card that is open (intro etc.) by tapping its primary button, like a player would.
    for (let i = 0; i < 3; i++) {
        const btn = await cdp.eval(`(() => { const b = [...document.querySelectorAll('.overlay:not(.hidden) button.primary')]
            .find(b => b.offsetParent); if (!b) return null; const r = b.getBoundingClientRect();
            return { x: r.x + r.width / 2, y: r.y + r.height / 2, id: b.id }; })()`);
        if (!btn) break;
        console.log(`        tapping card button #${btn.id}`);
        await tap(cdp, btn);
        await sleep(400);
    }
    const before = await cdp.eval(STATE);
    if (!check(!!before, 'UI.game exists')) return;
    // 1) tap the first shop item, then the best tile for it (scored with the autopilot's tileScore).
    const plan = await cdp.eval(`(() => {
        const g = UI.game, type = UI.st.shopOrder[0], item = UI.st.shopItems[0];
        if (!item) return { err: 'no shop items' };
        const ir = item.getBoundingClientRect();
        const visible = ir.width > 0 && ir.height > 0 && ir.right <= innerWidth && ir.bottom <= innerHeight;
        const ai = new Autopilot(g); let best = -1, col = -1, row = -1;
        for (let r = 0; r < g.rows; r++) for (let c = 0; c < g.cols; c++) {
            if (!g.canPlace(c, r)) continue; const s = ai.tileScore(type, c, r); if (s > best) { best = s; col = c; row = r; } }
        const cr = document.getElementById('board').getBoundingClientRect();
        const p = View.worldToScreen((col + 0.5) * DATA.TILE, (row + 0.5) * DATA.TILE);
        return { type, name: DATA.TOWERS[type].id, visible, item: { x: ir.x + ir.width / 2, y: ir.y + ir.height / 2 },
                 tile: { x: cr.x + p.x, y: cr.y + p.y }, col, row, coins: g.coins, cost: DATA.TOWERS[type].levels[0].cost };
    })()`);
    if (plan.err) { check(false, plan.err); return; }
    check(plan.visible, `shop item "${plan.name}" is on screen (at ${plan.item.x | 0},${plan.item.y | 0})`);
    await tap(cdp, plan.item);
    await sleep(150);
    const placing = await cdp.eval('UI.view.placing');
    check(placing === plan.type, `tap shop item -> placing ${plan.name} (UI.view.placing = ${placing})`);
    // Fresh screen position of the target tile (the camera may move: tap-to-zoom, pinch, pan).
    const tilePos = () => cdp.eval(`(() => { const cr = document.getElementById('board').getBoundingClientRect();
        const p = View.worldToScreen(${plan.col} * 64 + 32, ${plan.row} * 64 + 32); return { x: cr.x + p.x, y: cr.y + p.y }; })()`);
    const findPlaced = () => cdp.eval(`(() => { const t = UI.game.towers.find(t => t.col === ${plan.col} && t.row === ${plan.row}); return t ? { id: t.id, type: t.type } : null; })()`);
    const tilePx = await cdp.eval('View.scale * 64');
    if (tilePx < 34) {
        // Small tiles on a phone: the first tap only zooms in on the spot ("tap again to plant").
        const z0 = await cdp.eval('View.zoom');
        await tap(cdp, plan.tile);
        await sleep(250);
        const z1 = await cdp.eval('View.zoom'), early = await findPlaced();
        const toast = await cdp.eval(`(document.getElementById('toast') || {}).textContent || ''`);
        check(z1 > z0 && !early, `small tiles (${tilePx.toFixed(1)} px): tap 1 zooms in (View.zoom ${z0.toFixed(2)} -> ${z1.toFixed(2)}, tiles ${(z1 / z0 * tilePx).toFixed(1)} px), no plant yet; toast "${toast.trim()}"`);
        plan.tile = await tilePos();
    } else {
        console.log(`        tiles are ${tilePx.toFixed(1)} px: one tap should plant`);
    }
    await tap(cdp, plan.tile);
    await sleep(200);
    const placed = await findPlaced();
    check(!!placed && placed.type === plan.type, `tap tile ${plan.col},${plan.row} -> ${plan.name} planted (coins ${plan.coins} -> ${await cdp.eval('UI.game.coins')})`);
    await cdp.eval('UI.view.placing = -1');
    plan.tile = await tilePos();
    // 2) tap the plant -> selected; double-tap -> group of its kind
    if (placed) {
        await sleep(400);
        await tap(cdp, plan.tile);
        await sleep(400);   // let a double-tap window pass
        const sel = await cdp.eval('UI.view.selectedId');
        check(sel === placed.id, `tap plant -> selected (UI.view.selectedId = ${sel})`);
        await tap(cdp, plan.tile); await sleep(80); await tap(cdp, plan.tile);
        await sleep(400);
        const group = await cdp.eval('UI.view.group.length');
        check(group >= 1, `double-tap plant -> group of its kind (UI.view.group.length = ${group})`);
        await cdp.eval('UI.cancel && UI.cancel(); UI.cancel && UI.cancel();');
    }
    // 3) long-press on the plant -> hover/info
    await tap(cdp, plan.tile, 900);
    await sleep(200);
    const hover = await cdp.eval('JSON.stringify(UI.view.hover)');
    check(hover && hover !== 'null', `long-press -> hover/info (UI.view.hover = ${hover})`);
    // 4) pinch zoom out->in around the board center (from the fitted view)
    await cdp.eval('View.reset()');
    const c =await cdp.eval(`(() => { const r = document.getElementById('board').getBoundingClientRect(); return { x: r.x + r.width / 2, y: r.y + r.height / 2, z: View.zoom }; })()`);
    const pts = d => [{ x: c.x - d, y: c.y }, { x: c.x + d, y: c.y }];
    await touch(cdp, 'touchStart', pts(30));
    for (let d = 40; d <= 130; d += 15) { await sleep(30); await touch(cdp, 'touchMove', pts(d)); }
    await touch(cdp, 'touchEnd', []);
    await sleep(200);
    const z = await cdp.eval('View.zoom');
    check(z > c.z * 1.5, `pinch out -> View.zoom ${c.z.toFixed(2)} -> ${z.toFixed(2)}`);
    // 5) one-finger drag pans when zoomed
    const o = await cdp.eval('({ ox: View.ox, oy: View.oy })');
    await touch(cdp, 'touchStart', [{ x: c.x, y: c.y }]);
    for (let d = 10; d <= 100; d += 10) { await sleep(20); await touch(cdp, 'touchMove', [{ x: c.x + d, y: c.y + d / 2 }]); }
    await touch(cdp, 'touchEnd', []);
    await sleep(150);
    const o2 = await cdp.eval('({ ox: View.ox, oy: View.oy, plants: UI.game.towers.length })');
    check(Math.hypot(o2.ox - o.ox, o2.oy - o.oy) > 20, `1-finger drag when zoomed -> pan (${(o2.ox - o.ox) | 0},${(o2.oy - o.oy) | 0} px)`);
    check(o2.plants === (await cdp.eval(STATE)).plants, 'pan did not plant/act');
}

async function main() {
    if (!fs.existsSync(page)) throw new Error('no page ' + page);
    console.log(`browser-run: ${url}  [${size} ${W}x${H}${MOBILE ? ' mobile+touch' : ''}]`);
    const { proc, dir, port } = await launch();
    let cdp;
    try {
        const list = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
        const target = list.find(t => t.type === 'page');
        cdp = new Cdp(target.webSocketDebuggerUrl);
        await cdp.ready;
        const errors = [];
        cdp.on((method, p) => {
            if (method === 'Runtime.exceptionThrown') {
                const d = p.exceptionDetails;
                errors.push(`uncaught: ${d.exception?.description?.split('\n').slice(0, 3).join(' | ') || d.text} (${(d.url || '').split('/').pop()}:${d.lineNumber + 1})`);
            } else if (method === 'Runtime.consoleAPICalled' && (p.type === 'error' || p.type === 'assert')) {
                const f = p.stackTrace?.callFrames?.[0];
                errors.push(`console.${p.type}: ${p.args.map(a => a.value ?? a.description ?? a.type).join(' ')}${f ? ` (${f.url.split('/').pop()}:${f.lineNumber + 1})` : ''}`);
            } else if (method === 'Runtime.consoleAPICalled' && p.type === 'warning') {
                console.log(`${stamp()} console.warn: ${p.args.map(a => a.value ?? a.description).join(' ')}`);
            } else if (method === 'Log.entryAdded' && p.entry.level === 'error') {
                errors.push(`log: ${p.entry.text} ${p.entry.url ? '(' + p.entry.url.split('/').pop() + ')' : ''}`);
            }
        });
        await cdp.send('Runtime.enable');
        await cdp.send('Log.enable');
        await cdp.send('Page.enable');
        await cdp.send('Emulation.setDeviceMetricsOverride', { width: W, height: H, deviceScaleFactor: MOBILE ? 3 : 1, mobile: MOBILE,
            screenOrientation: MOBILE ? { type: 'landscapePrimary', angle: 90 } : { type: 'landscapePrimary', angle: 0 } });
        if (MOBILE) await cdp.send('Emulation.setTouchEmulationEnabled', { enabled: true, maxTouchPoints: 5 });
        // Until renderer.js lands: if no Renderer global exists after the page's scripts ran, stub it
        // (every method a no-op) so the UI can still be tested. Reported below; --no-stub disables.
        if (!flag('no-stub')) await cdp.send('Page.addScriptToEvaluateOnNewDocument', { source:
            `addEventListener('DOMContentLoaded', () => { if (typeof Renderer === 'undefined') {
                window.Renderer = new Proxy({}, { get: (o, k) => k in o ? o[k] : () => {} }); window.__rendererStubbed = true; } });` });
        const loaded = new Promise(r => cdp.on(m => m === 'Page.loadEventFired' && r()));
        await cdp.send('Page.navigate', { url });
        await Promise.race([loaded, sleep(10000)]);
        await sleep(300);
        console.log(`${stamp()} loaded: ${fmt(await cdp.eval(STATE))}`);
        if (await cdp.eval('!!window.__rendererStubbed')) console.log('  [note] js/renderer.js missing: Renderer STUBBED (no drawing)');

        if (flag('touch')) await touchTests(cdp);

        if (campaign) {
            let lastMap = 0, s = null;
            const end = Date.now() + seconds * 1000;
            while (Date.now() < end) {
                await sleep(1000);
                s = await cdp.eval(STATE);
                if (s && s.map !== lastMap) { if (lastMap) console.log(`${stamp()} reached map ${s.map} with ${s.lives} lives`); lastMap = s.map; }
                if (s && (s.phase === 'Won' || s.phase === 'Lost')) break;
            }
            console.log(`${stamp()} campaign end: ${fmt(s)}`);
            check(s && (s.phase === 'Won' || s.phase === 'Lost'), `campaign finished within ${seconds} s`);
            check(s && s.phase === 'Won', 'autopilot won the campaign in the browser');
        } else if (!flag('touch')) {
            await sleep(seconds * 1000);
        }
        console.log(`${stamp()} final: ${fmt(await cdp.eval(STATE))}`);
        if (opt('eval')) console.log(`${stamp()} eval: ${JSON.stringify(await cdp.eval(opt('eval')))}`);
        const shot = opt('shot');
        if (shot) {
            const r = await cdp.send('Page.captureScreenshot', { format: 'png' });
            fs.writeFileSync(path.resolve(ROOT, shot), Buffer.from(r.data, 'base64'));
            console.log(`${stamp()} screenshot ${shot}`);
        }
        const counts = new Map();
        errors.forEach(e => counts.set(e, (counts.get(e) || 0) + 1));
        counts.forEach((n, e) => console.log(`  ${n > 1 ? n + 'x ' : ''}${e}`));
        check(errors.length === 0, `no console errors / uncaught exceptions (${errors.length})`);
    } finally {
        try { cdp && cdp.ws.close(); } catch (e) {}
        proc.kill('SIGKILL');
        setTimeout(() => { try { fs.rmSync(dir, { recursive: true, force: true }); } catch (e) {} }, 300);
    }
    console.log(failures ? `${failures} FAILED` : 'ALL OK');
    process.exitCode = failures ? 1 : 0;
}
main().catch(e => { console.error('browser-run: ' + e.message); process.exitCode = 1; });
