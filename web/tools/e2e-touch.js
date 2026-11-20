#!/usr/bin/env node
// Garden Guard++ web: touch-only end-to-end test (owner: Dale). No autopilot, no keyboard: every
// action is a CDP touch on a real on-screen element. Before each tap it checks the target is at least
// 32 css px each way and that document.elementFromPoint(center) is the intended element (nothing
// on top). Test-only "cheats" (more coins, lives) go through UI.game and are listed in the output.
//
//   node tools/e2e-touch.js [--size iphone|android|pc] [--page dist/gardenguard-web/index.html] [--strict]
//   --strict: a target under 32 px or covered at its center is a FAIL (otherwise just reported)
'use strict';
const { ensureWebSocket, sleep, openPage } = require('./cdp');
ensureWebSocket(__filename);
const path = require('path');

const argv = process.argv.slice(2);
const opt = (n, d) => { const i = argv.indexOf('--' + n); return i >= 0 ? argv[i + 1] : d; };
const size = opt('size', 'iphone');
const page = path.resolve(__dirname, '..', opt('page', 'index.html'));
const MIN = 32;
const t0 = Date.now();
const stamp = () => ((Date.now() - t0) / 1000).toFixed(1).padStart(5) + 's';
let failures = 0;
const hard = new Map();   // hard-to-hit report: label -> issue
function check(ok, what) { console.log(`  ${ok ? '[ ok ]' : '[FAIL]'} ${what}`); if (!ok) failures++; return ok; }
function step(s) { console.log(`${stamp()} ${s}`); }

let cdp;
const ev = (e) => cdp.eval(e);
const G = (e) => ev(`(() => { const g = UI.game; return ${e}; })()`);

// Tap a DOM element (selector, or an element found by a JS expression) after checking it is hittable.
async function tapEl(sel, label = sel, { expr } = {}) {
    const find = expr || `document.querySelector(${JSON.stringify(sel)})`;
    const info = await ev(`(() => { const el = ${find}; if (!el) return null;
        const r = el.getBoundingClientRect(); const x = r.x + r.width / 2, y = r.y + r.height / 2;
        const top = document.elementFromPoint(x, y);
        const ok = !!top && (top === el || el.contains(top));
        const desc = (n) => n ? n.tagName.toLowerCase() + (n.id ? '#' + n.id : '') + (n.className && typeof n.className === 'string' ? '.' + n.className.trim().split(/\\s+/).join('.') : '') : 'nothing';
        return { x, y, w: r.width, h: r.height, ok, top: desc(top), visible: r.width > 0 && r.height > 0 && x >= 0 && y >= 0 && x <= innerWidth && y <= innerHeight, disabled: !!el.disabled }; })()`);
    if (!info) { check(false, `${label}: element not found`); return false; }
    if (!info.visible) { check(false, `${label}: not visible/on screen (${info.w | 0}x${info.h | 0} at ${info.x | 0},${info.y | 0})`); return false; }
    if (info.w < MIN || info.h < MIN) hard.set(label, `small target ${info.w.toFixed(0)}x${info.h.toFixed(0)} px`);
    if (!info.ok) hard.set(label, `covered at its center by ${info.top}`);
    if (info.disabled) console.log(`        (note: ${label} is disabled)`);
    await cdp.tap({ x: info.x, y: info.y });
    await sleep(250);
    return true;
}

// Screen point of a tile / world point (css px of the viewport).
async function worldPt(wx, wy) {
    return ev(`(() => { const r = document.getElementById('board').getBoundingClientRect(); const p = View.worldToScreen(${wx}, ${wy});
        const x = r.x + p.x, y = r.y + p.y; const top = document.elementFromPoint(x, y);
        return { x, y, onBoard: top && top.id === 'board', top: top ? top.tagName + '#' + top.id : 'nothing' }; })()`);
}
// Tap a tile; if the first tap only zoomed in (small tiles), tap the tile again at its new position.
async function tapTile(col, row, done, label) {
    for (let i = 0; i < 2; i++) {
        const z = await ev('View.zoom');
        const p = await worldPt(col * 64 + 32, row * 64 + 32);
        if (!p.onBoard) hard.set(label || `tile ${col},${row}`, `board covered by ${p.top}`);
        await cdp.tap(p);
        await sleep(250);
        if (await done()) return true;
        if (!(await ev('View.zoom') > z)) return false;
        console.log(`        (tap zoomed in first: View.zoom ${z.toFixed(2)} -> ${(await ev('View.zoom')).toFixed(2)})`);
    }
    return !!(await done());
}
async function doubleTapTile(col, row) {
    const p = await worldPt(col * 64 + 32, row * 64 + 32);
    await cdp.tap(p); await sleep(90); await cdp.tap(p); await sleep(300);
}
async function best(type) {   // best free tile for a plant, scored by the autopilot
    return G(`(() => { const ai = new Autopilot(g); let b = -1, c0 = -1, r0 = -1;
        for (let r = 0; r < g.rows; r++) for (let c = 0; c < g.cols; c++) { if (!g.canPlace(c, r)) continue;
            const s = ai.tileScore(${type}, c, r); if (s > b) { b = s; c0 = c; r0 = r; } } return { col: c0, row: r0 }; })()`);
}
const overlay = () => ev(`[...document.querySelectorAll('.overlay')].filter(o => !o.classList.contains('hidden')).map(o => o.id).join(',')`);
async function waitFor(expr, ms, label) {
    const end = Date.now() + ms;
    while (Date.now() < end) { if (await ev(expr)) return true; await sleep(250); }
    return check(false, `timed out waiting for ${label || expr}`);
}
async function plant(shopIdx, label) {
    const type = await ev(`UI.st.shopOrder[${shopIdx}]`), id = await ev(`DATA.TOWERS[${type}].id`);
    // Placing stays on while affordable (as in Qt), and tapping the item again would cancel it.
    if (await ev('UI.view.placing') !== type)
        await tapEl(null, `shop item ${shopIdx + 1} (${id})`, { expr: `UI.st.shopItems[${shopIdx}]` });
    if (!check(await ev('UI.view.placing') === type, `placing ${id}`)) return null;
    const t = await best(type);
    const n0 = await G('g.towers.length');
    const ok = await tapTile(t.col, t.row, () => G(`g.towers.length > ${n0}`), `${label || id} tile`);
    check(ok, `tap tile ${t.col},${t.row} -> ${id} planted (coins ${await G('g.coins')})`);
    return ok ? { ...t, type, id: await G(`g.towerAt(${t.col}, ${t.row}).id`) } : null;
}

async function main() {
    const url = 'file://' + page;
    console.log(`e2e-touch: ${url} [${size}]`);
    const P = await openPage(url, size);
    cdp = P.cdp;
    try {
        // 1) Intro: pick Hard, Let's grow, dismiss the new-bugs card
        step('1. intro: Hard + Let\'s grow');
        check((await overlay()).includes('ovIntro'), `intro card open (${await overlay()})`);
        await tapEl(null, 'difficulty Hard', { expr: `[...document.querySelectorAll('#difficultyHost button')].find(b => /Hard$/.test(b.textContent.trim()))` });
        check(await ev(`Difficulty.get()`) === 2 && await G('g.difficulty') === 2, `Hard selected (Difficulty.get() = ${await ev('Difficulty.get()')}, game.difficulty = ${await G('g.difficulty')}, coins ${await G('g.coins')})`);
        await tapEl('#btnStart', "Let's grow");
        if ((await overlay()).includes('ovBugs')) await tapEl('#btnBugs', 'new-bugs card button');
        check(await overlay() === '', `no card open (${await overlay() || 'none'})`);
        check(await G('g.coins') === 162, `Hard start coins 162 (180 x 0.9): ${await G('g.coins')}`);

        // 2) plant 3, start wave, upgrade one, sell one
        step('2. plant x3, start wave, upgrade, sell');
        const p1 = await plant(0), p2 = await plant(0), p3 = await plant(0);
        check(await G('g.towers.length') === 3, `3 plants (${await G('g.towers.length')})`);
        if (await ev('UI.view.placing') >= 0) {   // stop planting the touch way: tap the checked shop item
            await tapEl(null, 'shop item 1 (stop placing)', { expr: `UI.st.shopItems[0]` });
            check(await ev('UI.view.placing') === -1, 'tap checked shop item -> placing off');
        }
        await tapEl('#startWave', 'Start wave');
        check(await G('g.phase') === 'Wave', `wave started (phase ${await G('g.phase')}, wave ${await G('g.wave')})`);
        console.log('        [cheat] coins += 300 for the upgrade');
        await G('(g.coins += 300)');
        await tapTile(p1.col, p1.row, () => ev(`UI.view.selectedId === ${p1.id}`), 'select plant');
        check(await ev('UI.view.selectedId') === p1.id, `tap plant -> plant panel (selectedId ${await ev('UI.view.selectedId')})`);
        await tapEl('#tpUpgrade', 'Upgrade');
        check(await G(`g.getTower(${p1.id}).level`) === 1, `upgraded to level ${await G(`g.getTower(${p1.id}).level`) + 1}`);
        await tapTile(p3.col, p3.row, () => ev(`UI.view.selectedId === ${p3.id}`), 'select plant');
        await tapEl('#tpSell', 'Sell');
        check(!(await G(`g.getTower(${p3.id})`)) && await G('g.towers.length') === 2, `sold (plants ${await G('g.towers.length')})`);

        // 3) fire point: 🎯, tap a path tile in range, then clear
        step('3. fire point');
        await tapTile(p1.col, p1.row, () => ev(`UI.view.selectedId === ${p1.id}`), 'select plant');
        await tapEl('#tpAim', '🎯 Set fire point');
        check(await ev('UI.view.aimingId') === p1.id, `aiming (aimingId ${await ev('UI.view.aimingId')}, button "${await ev("document.getElementById('tpAim').textContent")}")`);
        const fp = await G(`(() => { const t = g.getTower(${p1.id}), R = DATA.TOWERS[t.type].levels[t.level].range; let b = null;
            for (let r = 0; r < g.rows; r++) for (let c = 0; c < g.cols; c++) if (g.isPathTile(c, r)) { const d = Math.hypot(c - t.col, r - t.row);
                if (d <= R - 0.6 && (!b || d > b.d)) b = { col: c, row: r, d }; } return b; })()`);
        await tapTile(fp.col, fp.row, () => G(`g.getTower(${p1.id}).hasFirePoint`), 'fire point tile');
        check(await G(`g.getTower(${p1.id}).hasFirePoint`), `fire point set on path tile ${fp.col},${fp.row}`);
        await tapEl('#tpAim', '🎯 Clear fire point');
        check(!(await G(`g.getTower(${p1.id}).hasFirePoint`)), 'fire point cleared');

        // 4) double-tap group, tap a bug -> focus, cancel
        step('4. group + focus fire + cancel');
        await ev('UI.cancel(); UI.cancel();');
        await doubleTapTile(p1.col, p1.row);
        const grp = await ev('UI.view.group.length');
        check(grp === 2, `double-tap -> group of ${grp} sprouts`);
        await waitFor(`UI.game.enemies.some(e => e.alive && e.x > 0 && e.y > 0 && e.x < UI.game.W && e.y < UI.game.H)`, 20000, 'a bug on the board');
        let focused = false;
        for (let i = 0; i < 5 && !focused; i++) {
            const bug = await G(`(() => { const e = g.enemies.filter(e => e.alive && e.x > 0 && e.x < g.W && e.y > 0 && e.y < g.H).sort((a, b) => b.dist - a.dist)[0]; return e ? { id: e.id, x: e.x, y: e.y } : null; })()`);
            if (!bug) { await sleep(300); continue; }
            const p = await worldPt(bug.x, bug.y);
            await cdp.tap(p);
            await sleep(200);
            focused = await G(`g.towers.some(t => t.focusId === ${bug.id})`);
            if (focused) check(true, `tap bug #${bug.id} -> focus fire (${await G(`g.towers.filter(t => t.focusId === ${bug.id}).length`)} plants)`);
        }
        if (!focused) check(false, `tap bug -> focus fire (selectedId ${await ev('UI.view.selectedId')}, group ${await ev('UI.view.group.length')})`);
        if (focused) {
            await tapEl('#tpCancelFocus', '✕ Cancel focus');
            check(await G('g.towers.every(t => t.focusId < 0)'), 'focus fire cancelled');
        }
        await ev('UI.cancel(); UI.cancel(); UI.cancel();');

        // 5) speed, pause, side toggle, journal, zoom reset
        step('5. speed / pause / side / journal / zoom');
        const speeds = [];
        for (let i = 0; i < 3; i++) { await tapEl('#btnSpeed', 'Speed'); speeds.push(await ev('UI.st.speed')); }
        check(speeds.join() === '2,3,1', `speed button cycles 1 -> ${speeds.join(' -> ')}`);
        await tapEl('#btnPause', 'Pause');
        const paused = await ev('UI.paused()'), tA = await G('g.time'); await sleep(500);
        check(paused && await G('g.time') === tA, `paused (game.time frozen at ${tA.toFixed(2)})`);
        await tapEl('#btnPause', 'Resume');
        check(!(await ev('UI.paused()')), 'resumed');
        const w0 = await ev(`document.getElementById('board').getBoundingClientRect().width`);
        await tapEl('#sideToggle', '▸ side toggle');
        await sleep(400);
        const w1 = await ev(`document.getElementById('board').getBoundingClientRect().width`);
        const coll = await ev(`document.getElementById('side').classList.contains('collapsed')`);
        check(coll && w1 > w0 + 50, `side collapsed, board ${w0 | 0} -> ${w1 | 0} px wide`);
        await tapEl('#sideToggle', '◂ side toggle');
        await sleep(400);
        const w2 = await ev(`document.getElementById('board').getBoundingClientRect().width`);
        check(!(await ev(`document.getElementById('side').classList.contains('collapsed')`)) && Math.abs(w2 - w0) < 2, `side expanded, board back to ${w2 | 0} px`);
        await tapEl('#btnJournal', '📖 Journal');
        check((await overlay()).includes('ovJournal'), `journal open (${await overlay()})`);
        await tapEl('#btnJournalClose', 'Journal Close');
        check(await overlay() === '', 'journal closed');
        const c = await ev(`(() => { const r = document.getElementById('board').getBoundingClientRect(); return { x: r.x + r.width / 2, y: r.y + r.height / 2 }; })()`);
        const pts = d => [{ x: c.x - d, y: c.y }, { x: c.x + d, y: c.y }];
        await cdp.touch('touchStart', pts(30));
        for (let d = 40; d <= 120; d += 20) { await sleep(30); await cdp.touch('touchMove', pts(d)); }
        await cdp.touch('touchEnd', []);
        await sleep(300);
        check(await ev('View.isZoomed()'), `pinch -> zoomed (View.zoom ${(await ev('View.zoom')).toFixed(2)})`);
        await tapEl('#btnZoomReset', '🔍 zoom reset');
        check(!(await ev('View.isZoomed()')), `zoom reset -> View.zoom ${(await ev('View.zoom')).toFixed(2)}`);

        // 6) play to map cleared at 3x, On to..., dismiss new-bugs card
        step('6. play to map cleared');
        console.log('        [cheat] lives = 999 so the 2-sprout garden survives Hard');
        await G('(g.lives = 999)');
        await tapEl('#btnSpeed', 'Speed'); await tapEl('#btnSpeed', 'Speed');
        check(await ev('UI.st.speed') === 3, `speed 3 (${await ev('UI.st.speed')})`);
        const endWait = Date.now() + 240000;
        while (Date.now() < endWait) {
            const ph = await G('g.phase');
            if (ph === 'MapCleared' || (await overlay()).includes('ovNext') || ph === 'Lost') break;
            if (ph === 'Build' && await ev(`!document.getElementById('startWave').disabled`)) await tapEl('#startWave', 'Start wave');
            await sleep(500);
        }
        await waitFor(`!document.getElementById('ovNext').classList.contains('hidden')`, 15000, 'map-cleared card');
        await sleep(300);
        check((await overlay()).includes('ovNext'), `map-cleared card (${await overlay()}): "${(await ev("document.getElementById('nextTitle').textContent")).replace(/\n/g, ' ')}"`);
        const btnNext = await ev(`document.getElementById('btnNext').textContent`);
        check(/^On to /.test(btnNext), `button "${btnNext}"`);
        await tapEl('#btnNext', btnNext);
        check(await G('g.mapIndex') === 1, `map 2 (mapIndex ${await G('g.mapIndex')})`);
        if ((await overlay()).includes('ovBugs')) {
            check(true, `new-bugs card: "${await ev("document.getElementById('bugsTitle').textContent")}"`);
            await tapEl('#btnBugs', 'new-bugs card button');
        }
        check(await overlay() === '', `cards closed (${await overlay() || 'none'})`);

        // 7) lose on purpose, end card, Play again
        step('7. lose on purpose');
        console.log('        [cheat] lives = 1, no plants on map 2');
        await G('(g.lives = 1)');
        await tapEl('#startWave', 'Start wave');
        await waitFor(`UI.game.phase === 'Lost'`, 120000, 'phase Lost');
        await sleep(500);
        check((await overlay()).includes('ovEnd'), `end card open (${await overlay()}): "${await ev("document.getElementById('endTitle').textContent")}"`);
        await tapEl('#btnAgain', 'Play again');
        const s = await G('({ map: g.mapIndex, lives: g.lives, wave: g.wave, phase: g.phase, plants: g.towers.length, coins: g.coins })');
        check(s.map === 0 && s.wave === 0 && s.phase === 'Build' && s.plants === 0 && s.lives === 20,
            `fresh run: map ${s.map + 1} wave ${s.wave} ${s.phase} lives ${s.lives} plants ${s.plants} coins ${s.coins} (cards: ${await overlay() || 'none'})`);
        await sleep(300);
        await cdp.send('Page.captureScreenshot', { format: 'png' }).then(r => require('fs').writeFileSync(path.join(__dirname, '..', 'shots', `_e2e-${size}.png`), Buffer.from(r.data, 'base64')));
    } catch (e) {
        check(false, 'exception: ' + e.message);
    } finally {
        const counts = new Map();
        P.errors.forEach(e => counts.set(e, (counts.get(e) || 0) + 1));
        counts.forEach((n, e) => console.log(`  ${n > 1 ? n + 'x ' : ''}${e}`));
        check(P.errors.length === 0, `no console errors / uncaught exceptions (${P.errors.length})`);
        console.log(hard.size ? 'Hard to hit:' : 'Hard to hit: nothing (all targets >= 32 px and on top)');
        hard.forEach((v, k) => console.log(`  - ${k}: ${v}`));
        if (argv.includes('--strict')) check(hard.size === 0, `every tapped target >= ${MIN} px and on top (--strict): ${hard.size} issue(s)`);
        P.close();
    }
    console.log(failures ? `${failures} FAILED` : 'ALL OK');
    process.exitCode = failures ? 1 : 0;
}
main().catch(e => { console.error('e2e-touch: ' + e.message); process.exitCode = 1; });
