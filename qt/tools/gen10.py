"""Generate data::MAPS for Garden Guard++ (10 maps) and splice it into data.h.
Usage: python3 tools/gen10.py [--dry] [--report] [--season] [M=hpScale,coins ...]
Without --dry it REWRITES the MAPS block in $GG_ROOT/src/data.h (default: this project)."""
import math, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from maps10 import MAPS as LAYOUT, check

SEASON = ["Spring", "Spring", "Summer", "Summer", "Summer", "Autumn", "Autumn", "Winter", "Winter", "Winter"]
COLORS = [("#79c25a", "#73bb54"), ("#8cc35a", "#86bc52"), ("#6aae55", "#64a74f"), ("#b5b04c", "#ada846"),
          ("#2f5f42", "#2b583d"), ("#c99a45", "#c1923f"), ("#b98a3e", "#b08238"), ("#a9c3b8", "#a1bbb0"),
          ("#dfe9ee", "#d6e1e7"), ("#c9dbe0", "#c0d3d8")]
SEEDS = [20261118, 7051, 4242, 1031, 9001, 6161, 3110, 1225, 2412, 3030]
COINS = [180, 320, 360, 500, 500, 560, 620, 680, 740, 800]
BLURBS = [
 "A gentle meadow to get your garden growing.",
 "Butterflies flutter along the sky lane, out of reach of mortars and chilis. Caterpillars split when squashed!",
 "Crickets leap ahead and scorpions spit goo that stuns plants: don't crowd the path. A snail guards the ridge.",
 "Golden late-summer fields. Mosquitoes heal the swarm, so take them out first. Watch for a golden glint!",
 "Night falls. Fireflies light the way, and a wild bat flaps unpredictably across the garden. Tip: a Frost Fern where a lane crosses the path slows bugs on the ground and in the air.",
 "Autumn leaves and long switchbacks. The swarms are bigger now, a wild bat may swoop through, and gusts of wind blow flyers off course.",
 "Crossroads! Bugs come from two entrances, the left edge and the top, and their paths cross three times. Plants at a crossing hit both streams. Watch the windy gusts.",
 "Winter arrives. Deep snow slows the bugs on the ground (but not flyers), and frost bites less in the cold. The path doubles back on itself, so plants at the bends hit bugs twice.",
 "Deep snow slows the walkers, but frost is weaker in the cold. Three sky lanes, a frozen pond and two snails: every bug you've met is here.",
 "The grand finale on the snowy summit: the longest path, the biggest swarms and three snails. Deep snow slows the bugs, and frost is weaker. Guard the garden!",
]
NIGHT = [False] * 4 + [True] + [False] * 5

# Hand-tuned waves for maps 1-5: (hp, [(bug, count, gap, delay), ...])
W = {
 0: [(1.00, [("Ant", 10, 0.9, 0)]),
     (1.20, [("Ant", 14, 0.7, 0), ("Beetle", 2, 2.5, 6)]),
     (1.40, [("Wasp", 10, 0.5, 0), ("Ant", 8, 0.7, 5)]),
     (1.70, [("Beetle", 6, 1.4, 0), ("Ant", 10, 0.6, 2), ("Wasp", 6, 0.5, 8)]),
     (2.00, [("Ant", 20, 0.3, 0), ("Wasp", 14, 0.35, 3), ("Beetle", 6, 1.0, 6)])],
 1: [(0.84, [("Ant", 12, 0.7, 0), ("Caterpillar", 2, 3.0, 5)]),
     (1.05, [("Butterfly", 8, 0.8, 0), ("Ant", 10, 0.6, 4)]),
     (1.26, [("Beetle", 6, 1.4, 0), ("Caterpillar", 4, 2.0, 3), ("Butterfly", 6, 0.7, 8)]),
     (1.47, [("Wasp", 14, 0.4, 0), ("Butterfly", 10, 0.6, 4), ("Caterpillar", 3, 2.5, 8)]),
     (1.68, [("Ant", 16, 0.4, 0), ("Beetle", 8, 1.0, 2), ("Butterfly", 14, 0.5, 6), ("Caterpillar", 4, 2.0, 10)])],
 2: [(0.84, [("Cricket", 6, 1.2, 0), ("Ant", 12, 0.6, 3)]),
     (1.02, [("Scorpion", 3, 3.0, 0), ("Beetle", 6, 1.3, 2), ("Butterfly", 6, 0.8, 6)]),
     (1.20, [("Cricket", 8, 1.0, 0), ("Wasp", 12, 0.4, 4), ("Caterpillar", 3, 2.5, 8)]),
     (1.44, [("Scorpion", 5, 2.5, 0), ("Butterfly", 10, 0.6, 3), ("Cricket", 6, 1.0, 8)]),
     (1.68, [("Ant", 18, 0.4, 0), ("Beetle", 10, 0.9, 2), ("Snail", 1, 1, 6), ("Scorpion", 3, 3.0, 9),
             ("Butterfly", 10, 0.6, 10)])],
 # 4 Harvest Hollow: mosquito + golden beetle debut, no bats.
 3: [(0.86, [("Ant", 16, 0.4, 0), ("Butterfly", 8, 0.6, 3), ("Cricket", 6, 1.0, 6)]),
     (1.04, [("Beetle", 10, 1.0, 0), ("Scorpion", 3, 3.0, 4), ("Mosquito", 2, 4.0, 6)]),
     (1.22, [("Caterpillar", 6, 1.6, 0), ("Mosquito", 3, 3.5, 4), ("Wasp", 14, 0.35, 6), ("GoldenBeetle", 1, 1, 10)]),
     (1.31, [("Cricket", 10, 0.8, 0), ("Butterfly", 10, 0.6, 3), ("Scorpion", 3, 2.5, 6)]),
     (1.40, [("Ant", 20, 0.35, 0), ("Beetle", 12, 0.8, 2), ("Mosquito", 2, 4.0, 6), ("Snail", 1, 1, 8),
             ("Caterpillar", 3, 2.0, 12), ("Butterfly", 8, 0.6, 14)])],
 # 5 Moonlit Garden (night): first wild bats, exactly 2.
 4: [(0.80, [("Butterfly", 8, 0.7, 0), ("Ant", 14, 0.5, 2)]),
     (0.95, [("Mosquito", 2, 4.0, 0), ("Beetle", 8, 1.2, 0), ("Butterfly", 8, 0.6, 5)]),
     (1.15, [("Bat", 1, 1, 4), ("Cricket", 8, 1.0, 0), ("Butterfly", 6, 0.6, 3), ("GoldenBeetle", 1, 1, 9)]),
     (1.35, [("Scorpion", 4, 2.5, 0), ("Mosquito", 2, 3.5, 3), ("Butterfly", 8, 0.6, 6), ("Caterpillar", 3, 2.0, 8)]),
     (1.55, [("Beetle", 10, 0.8, 0), ("Bat", 1, 1, 6), ("Mosquito", 2, 4.0, 5), ("Wasp", 12, 0.35, 8),
             ("Snail", 1, 1, 10)])],
}

# Terrain from claude2-63: (col, row, kind) with 0 = rock, 1 = water, 2 = lily pad (plantable).
TERRAIN = {
 2: [(6, 1, 0), (7, 6, 0), (14, 9, 0), (2, 7, 0), (12, 1, 0), (15, 3, 0)],
 3: [(13, 8, 1), (14, 8, 1), (13, 9, 1), (14, 9, 1), (13, 10, 1), (14, 10, 1), (12, 10, 1), (12, 8, 2), (12, 9, 2)],
 4: [(6, 7, 1), (7, 7, 2), (8, 7, 1), (9, 7, 1), (6, 8, 1), (7, 8, 1), (8, 8, 2), (9, 8, 1)],
 5: [(7, 6, 0), (11, 8, 0), (17, 4, 0), (2, 9, 0), (20, 13, 0)],
 7: [(17, 6, 1), (18, 6, 1), (17, 7, 1), (18, 7, 1), (17, 8, 1), (18, 8, 1), (18, 9, 1), (16, 7, 2), (19, 8, 2)],
 8: [(6, 14, 1), (7, 14, 1), (8, 14, 1), (6, 15, 1), (7, 15, 1), (8, 15, 1), (9, 15, 1), (20, 4, 0), (25, 13, 0), (10, 8, 0)],
 9: [(8, 8, 0), (24, 6, 0), (14, 15, 0), (5, 14, 1), (6, 14, 1), (7, 14, 1), (6, 15, 1), (7, 15, 1), (8, 15, 1), (6, 16, 2)],
}
USE_TERRAIN = "--no-terrain" not in sys.argv
SNOW = "--no-snow" not in sys.argv
SNOW_SPEED, SNOW_FROST = 0.85, 0.8
# Second ground entrance (claude2-0c's MapDef::path2). Map 7 "crossroads": enters from the
# top edge, crosses the main path three times, then merges into its exit run.
PATH2 = {6: [(8, -1), (8, 9), (14, 9), (14, 14), (19, 14), (19, 9), (24, 9)]}
USE_PATH2 = "--no-path2" not in sys.argv
# Wind gusts (claude2-63's MapDef::windEvery): seconds between gusts on the autumn maps.
WIND = {5: 12.0, 6: 12.0}
USE_WIND = "--no-wind" not in sys.argv

def check_terrain():
    ok = True
    for m, ts in TERRAIN.items():
        name, cols, rows, path, _ = LAYOUT[m]
        segs, _ = __import__("maps10").segs_tiles(path, cols, rows)
        on_path = {t for s in segs for t in s}
        for c, r, k in ts:
            if not (0 <= c < cols and 0 <= r < rows): print(f"  terrain off-grid on map {m+1}: {(c, r)}"); ok = False
            if (c, r) in on_path: print(f"  terrain on path (ignored by game) on map {m+1}: {(c, r)}")
    return ok

CLIMAX_N = float(os.environ.get("CLIMAX_N", "1.3"))    # waves 4-5 swarm size, maps 6-10
CLIMAX_HP = float(os.environ.get("CLIMAX_HP", "1.1"))  # waves 4-5 hp, maps 6-10
OPEN_N = float(os.environ.get("OPEN_N", "1.0"))        # waves 1-2 swarm size, maps 9-10

BATS = {5: 1, 6: 2, 7: 1, 8: 2, 9: 2}        # map index -> wild bats in total (maps 6-10)
SNAILS = {5: 1, 6: 1, 7: 2, 8: 2, 9: 3}

def late_waves(m, hp_scale=1.0):
    """Template waves for maps 6-10 (index 5-9): bigger swarms on bigger maps."""
    k = 1 + 0.25 * (m - 4)                     # swarm size factor: 1.25 .. 2.25
    g = 1 / math.sqrt(k)                       # tighter gaps as swarms grow
    n = lambda c: max(1, round(c * k))
    base = 0.90 + 0.06 * (m - 5)
    hps = [round((base + 0.16 * i) * hp_scale, 2) for i in range(5)]
    bats = BATS[m]
    w = [
     [("Ant", n(16), 0.4 * g, 0), ("Butterfly", n(6), 0.6 * g, 3), ("Cricket", n(4), 1.0 * g, 6)],
     [("Beetle", n(8), 1.0 * g, 0), ("Scorpion", n(2), 3.0 * g, 4), ("Caterpillar", n(3), 2.0 * g, 6)],
     [("Wasp", n(14), 0.35 * g, 0), ("Mosquito", n(2), 3.5 * g, 4), ("Butterfly", n(8), 0.6 * g, 6),
      ("GoldenBeetle", 1, 1, 10)],
     [("Cricket", n(8), 0.8 * g, 0), ("Scorpion", n(3), 2.5 * g, 4), ("Caterpillar", n(3), 2.0 * g, 6),
      ("Butterfly", n(8), 0.5 * g, 10)] + ([("Bat", 1, 1, 8)] if bats >= 2 else []),
     [("Ant", n(18), 0.35 * g, 0), ("Beetle", n(10), 0.8 * g, 2), ("Mosquito", n(2), 4.0 * g, 6),
      ("Snail", SNAILS[m], 6, 8), ("Caterpillar", n(3), 2.0 * g, 12), ("Butterfly", n(8), 0.5 * g, 14),
      ("Bat", 1, 1, 16)],
    ]
    # Climax: waves 4-5 get bigger swarms and tougher bugs; maps 9-10 open more gently.
    # Bosses, bats and the golden beetle keep their exact counts.
    keep = {"Snail", "Bat", "GoldenBeetle"}
    def scale(groups, f):
        return [(b, c if b in keep else max(1, round(c * f)), gap, d) for b, c, gap, d in groups]
    for i in (3, 4):
        w[i] = scale(w[i], CLIMAX_N)
        hps[i] = round(hps[i] * CLIMAX_HP, 2)
    if m >= 8:
        for i in (0, 1):
            w[i] = scale(w[i], OPEN_N)
    return list(zip(hps, w))

def fmt_num(x):
    s = f"{x:.2f}".rstrip("0").rstrip(".")
    return s if "." in s else s + ".0" if False else s

def emit(tune, season):
    lines = ["inline const std::vector<MapDef> MAPS = {"]
    for m, (name, cols, rows, path, lanes) in enumerate(LAYOUT):
        hp_scale, coins = tune.get(m, (1.0, COINS[m]))
        waves = late_waves(m, hp_scale) if m >= 5 else [(round(h * hp_scale, 2), g) for h, g in W[m]]
        p = ", ".join(f"{{{c}, {r}}}" for c, r in path)
        ln = ", ".join("{" + ", ".join(f"{{{fmt_num(x)}, {fmt_num(y)}}}" for x, y in l) + "}" for l in lanes)
        lines.append(f"    // {m + 1}: {name} ({cols}x{rows}, {SEASON[m].lower()}{', night' if NIGHT[m] else ''})")
        lines.append(f'    {{"{name}", "{BLURBS[m]}",')
        lines.append(f"     {{{p}}},")
        a, b = COLORS[m]
        lines.append(f'     1.0, {coins}, "{a}", "{b}", {SEEDS[m]}, {"true" if NIGHT[m] else "false"},')
        lines.append(f"     {{{ln}}},")
        wl = []
        for hp, groups in waves:
            gs = ", ".join(f"{{{b}, {c}, {fmt_num(round(gap, 2))}, {fmt_num(d)}}}" for b, c, gap, d in groups)
            wl.append(f"      {{{hp:.2f}, {{{gs}}}}}")
        lines.append("     {\n" + ",\n".join(wl) + "},")
        terr = TERRAIN.get(m, [])
        tail = f"     {cols}, {rows}" + (f", Season::{SEASON[m]}" if season else "")
        winter = SEASON[m] == "Winter" and SNOW
        p2 = PATH2.get(m) if USE_PATH2 else None
        wind = WIND.get(m, 0) if USE_WIND else 0
        # Trailing MapDef fields in declaration order: terrain, bugSpeedMult, frostMult, path2, windEvery.
        # Emit up to the last non-default one (aggregate init is positional).
        trail = [
            ("{" + ", ".join(f"{{{c}, {r}, {k}}}" for c, r, k in (terr if USE_TERRAIN else [])) + "}",
             bool(terr and USE_TERRAIN)),
            (f"{SNOW_SPEED}" if winter else "1.0", winter),
            (f"{SNOW_FROST}" if winter else "1.0", winter),
            ("{" + ", ".join(f"{{{c}, {r}}}" for c, r in (p2 or [])) + "}", bool(p2)),
            (f"{wind:.1f}", wind > 0),
        ]
        last = max([i for i, (_, used) in enumerate(trail) if used], default=-1)
        if season and last >= 0:
            tail += ",\n     " + ", ".join(v for v, _ in trail[:last + 1])
        tail += "},"
        lines.append(tail)
    lines.append("};")
    return "\n".join(lines)

def bug_report():
    """Per-map bug usage, for the unlock table and the bat rule."""
    for m in range(10):
        waves = late_waves(m) if m >= 5 else W[m]
        bugs = sorted({b for _, g in waves for b, *_ in g})
        bats = sum(c for _, g in waves for b, c, *_ in g if b == "Bat")
        print(f"  map {m + 1:2d}: bats {bats}  bugs {', '.join(bugs)}")

if __name__ == "__main__":
    args = sys.argv[1:]
    tune = {}
    for a in args:
        if "=" in a:
            mi, v = a.split("=")
            h, c = v.split(",")
            tune[int(mi) - 1] = (float(h), int(c))
    assert all(check(*m) for m in LAYOUT)
    assert check_terrain()
    text = emit(tune, "--season" in args)
    if "--report" in args: bug_report()
    if "--dry" in args:
        print(text[:1500]); sys.exit()
    # Project root: $GG_ROOT, else the folder above tools/ (so it works from any cwd).
    root = os.environ.get("GG_ROOT", os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    path = os.path.join(root, "src", "data.h")
    src = open(path, encoding="utf-8").read()
    new, n = re.subn(r"inline const std::vector<MapDef> MAPS = \{.*?\n\};", lambda _: text, src, flags=re.S)
    if n != 1: sys.exit("MAPS block not found")
    open(path, "w", encoding="utf-8").write(new)
    print("MAPS written (10 maps)")
