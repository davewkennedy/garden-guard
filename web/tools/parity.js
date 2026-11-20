#!/usr/bin/env node
// Garden Guard++ web: Node parity check against the Qt build (owner: Dale).
// Runs the same headless autopilot games as tests/simtest.cpp (Test 3 and Test D) using
// js/data.js, js/game.js and js/autopilot.js, then compares with the C++ baseline in
// tools/baseline/ (captured from ~/projects/garden-plusplus/build/de/garden_simtest).
//
//   node tools/parity.js            # Normal campaign + Easy/Hard, summary + diff
//   node tools/parity.js --waves    # also diff every wave (lives, coins, plant list) vs C++
//   node tools/parity.js --normal   # Normal only (fast)
'use strict';
const fs = require('fs');
const path = require('path');

const ROOT = path.join(__dirname, '..');
const args = new Set(process.argv.slice(2));

function load(file, name) {
    const mod = require(path.join(ROOT, 'js', file));
    globalThis[name] = mod;
    return mod;
}
load('data.js', 'DATA');
load('game.js', 'Game');
load('autopilot.js', 'Autopilot');

const Phase = Game.Phase || { Build: 'Build', Wave: 'Wave', MapCleared: 'MapCleared', Won: 'Won', Lost: 'Lost' };
const Diff = Game.Difficulty || { Easy: 0, Normal: 1, Hard: 2 };

// C++ results (garden_simtest, build/de, 2026-11-20 14:31). Lives at the end of each map.
const CPP = {
    Easy:   { perMap: '20/20/20/20/20/20/20/20/18/18', result: 'WON' },
    Normal: { perMap: '20/20/20/18/18/16/14/14/12/6', result: 'WON',
              plants: [10, 12, 10, 10, 10, 12, 13, 15, 19, 19] },
    Hard:   { perMap: '19/19/12/4/4/0', result: 'LOST on map 6 wave 4' },
};

function towerList(g) {
    return g.towers.map(t => `${DATA.TOWERS[t.type].id}${t.level + 1}@${t.col},${t.row}`).join(' ');
}

// Same loop as simtest Test 3 / Test D: think every 30 frames of each 1 s chunk, dt = 1/60.
function play(difficulty) {
    const g = new Game();
    if (g.quiet !== undefined) g.quiet = true;
    if (difficulty !== undefined && g.setDifficulty) g.setDifficulty(difficulty);
    const ai = new Autopilot(g);
    const runFor = (seconds) => {
        for (let i = 0; i < Math.trunc(seconds * 60); i++) {
            if (i % 30 === 0) ai.think();
            g.update(1.0 / 60);
        }
    };
    const r = { perMap: [], plants: [], waves: [], maps: [], won: false };
    for (;;) {
        while (g.phase === Phase.Build) {
            runFor(1.0);
            if (!g.startWave()) break;
            for (let i = 0; i < 600 && g.phase === Phase.Wave; i++) runFor(1.0);
            r.waves.push(`      wave ${String(g.wave).padStart(2)}: lives ${String(g.lives).padStart(2)} coins ${String(g.coins).padStart(4)} plants: ${towerList(g)}`.trimEnd());
        }
        const status = g.phase === Phase.MapCleared ? 'cleared' : g.phase === Phase.Won ? 'WON' : 'LOST';
        const line = `    map ${g.mapIndex + 1} ${g.map().name.padEnd(14)} -> ${status} at wave ${g.wave}, lives ${g.lives}, plants ${g.towers.length}`;
        r.waves.push(line);
        r.maps.push(line);
        r.perMap.push(g.lives);
        r.plants.push(g.towers.length);
        if (g.phase !== Phase.MapCleared) break;
        g.nextMap();
    }
    r.won = g.phase === Phase.Won;
    r.result = r.won ? 'WON' : `LOST on map ${g.mapIndex + 1} wave ${g.wave}`;
    return r;
}

// Same game driven the browser way: a plain fixed-step loop calling ai.fixedStep() before each
// update(1/60), as main.js does. Must give the same numbers as play().
function playFixed(difficulty) {
    const g = new Game();
    if (g.quiet !== undefined) g.quiet = true;
    if (difficulty !== undefined && g.setDifficulty) g.setDifficulty(difficulty);
    const ai = new Autopilot(g);
    const r = { perMap: [], plants: [], waves: [], maps: [], won: false };
    let recorded = -1;
    for (let step = 0; step < 60 * 60 * 120; step++) {
        ai.fixedStep();
        g.update(1.0 / 60);
        const done = g.phase === Phase.MapCleared || g.phase === Phase.Won || g.phase === Phase.Lost;
        if (done && recorded !== g.mapIndex) {
            recorded = g.mapIndex;
            r.perMap.push(g.lives);
            r.plants.push(g.towers.length);
        }
        if (g.phase === Phase.Won || g.phase === Phase.Lost) break;
    }
    r.won = g.phase === Phase.Won;
    r.result = r.won ? 'WON' : `LOST on map ${g.mapIndex + 1} wave ${g.wave}`;
    return r;
}

let mismatches = 0;
function compare(name, r) {
    const js = r.perMap.join('/');
    const cpp = CPP[name];
    const same = js === cpp.perMap && r.result === cpp.result;
    if (!same) mismatches++;
    console.log(`  ${same ? '[ ok ]' : '[DIFF]'} ${name.padEnd(6)} JS  ${js} -> ${r.result}`);
    if (!same) console.log(`         ${''.padEnd(6)} C++ ${cpp.perMap} -> ${cpp.result}`);
    // Per-map delta in lives (positive = JS kept more lives).
    if (!same) {
        const c = cpp.perMap.split('/').map(Number);
        const d = [];
        for (let i = 0; i < Math.max(c.length, r.perMap.length); i++)
            d.push(i < c.length && i < r.perMap.length ? (r.perMap[i] - c[i] >= 0 ? '+' : '') + (r.perMap[i] - c[i]) : '?');
        console.log(`         delta  ${d.join(' ')}`);
    }
}

const t0 = Date.now();
console.log('Parity: JS port vs Qt garden_simtest');
const normal = play(Diff.Normal);
normal.maps.forEach(l => console.log(l));
compare('Normal', normal);
if (CPP.Normal.plants && normal.plants.join() !== CPP.Normal.plants.join())
    console.log(`         plants JS ${normal.plants.join('/')}  C++ ${CPP.Normal.plants.join('/')}`);

if (args.has('--waves')) {
    // Line-by-line diff of the per-wave log against AUTOPILOT_VERBOSE=1 simtest output.
    const base = fs.readFileSync(path.join(__dirname, 'baseline', 'de_verbose.txt'), 'utf8')
        .split('\n').filter(l => /^\s+(wave|map) /.test(l)).map(l => l.trimEnd());
    let first = -1;
    for (let i = 0; i < Math.max(base.length, normal.waves.length); i++)
        if ((base[i] || '') !== (normal.waves[i] || '')) { first = i; break; }
    if (first < 0) console.log('  [ ok ] every wave matches C++ exactly (lives, coins, plants)');
    else {
        console.log(`  [DIFF] first per-wave difference at line ${first + 1}:`);
        console.log(`    C++: ${base[first] || '(none)'}`);
        console.log(`    JS : ${normal.waves[first] || '(none)'}`);
        if (first > 0) console.log(`    (previous line matched: ${base[first - 1].trim().slice(0, 60)}...)`);
    }
}

if (args.has('--fixed')) {
    console.log('Browser-style driver (Autopilot.fixedStep before each update(1/60)):');
    const f = playFixed(Diff.Normal);
    compare('Normal', f);
    if (f.plants.join() !== CPP.Normal.plants.join()) console.log(`         plants JS ${f.plants.join('/')}  C++ ${CPP.Normal.plants.join('/')}`);
}

if (!args.has('--normal')) {
    compare('Easy', play(Diff.Easy));
    compare('Hard', play(Diff.Hard));
}
console.log(`${mismatches === 0 ? 'PARITY OK' : mismatches + ' mismatch(es)'} (${((Date.now() - t0) / 1000).toFixed(1)} s)`);
process.exitCode = mismatches === 0 ? 0 : 1;
