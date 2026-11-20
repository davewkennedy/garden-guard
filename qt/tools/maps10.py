"""Garden Guard++ 10-map layout: definitions + validator. Run: python3 maps10.py"""
import math

MAPS = [
 # name, cols, rows, path, lanes
 ("Sunny Meadow", 15, 10,
  [(-1, 1), (2, 1), (2, 6), (6, 6), (6, 2), (10, 2), (10, 8), (13, 8), (13, 4), (15, 4)], []),
 ("Apple Orchard", 16, 11,
  [(-1, 9), (3, 9), (3, 1), (7, 1), (7, 8), (12, 8), (12, 2), (16, 2)],
  [[(-1, 10), (5, 10), (10, 4.5), (16, 2)]]),
 ("Thorny Ridge", 17, 11,
  [(-1, 4), (1, 4), (1, 1), (4, 1), (4, 9), (9, 9), (9, 3), (12, 3), (12, 6), (17, 6)],
  [[(-1, 0), (7, 0.5), (11, 5.5), (17, 6)]]),
 ("Harvest Hollow", 18, 12,
  [(-1, 7), (3, 7), (3, 2), (7, 2), (7, 9), (11, 9), (11, 1), (15, 1), (15, 6), (18, 6)],
  [[(-1, 0), (9, 0.3), (13, 3.5), (18, 6)], [(-1, 11), (6, 10.5), (13, 7), (18, 6)]]),
 ("Moonlit Garden", 20, 13,
  [(-1, 1), (15, 1), (15, 5), (2, 5), (2, 10), (20, 10)],
  [[(-1, 3), (10, 3), (17, 7.5), (20, 10)], [(-1, 7.5), (11, 7.5), (20, 10)]]),
 ("Amber Woods", 22, 15,
  [(-1, 2), (4, 2), (4, 12), (9, 12), (9, 2), (14, 2), (14, 12), (19, 12), (19, 7), (22, 7)],
  [[(-1, 14), (7, 14), (12, 7), (22, 7)], [(-1, 0), (11, 0), (17, 5), (22, 7)]]),
 ("Pumpkin Patch", 24, 16,
  [(-1, 13), (5, 13), (5, 3), (11, 3), (11, 12), (17, 12), (17, 3), (21, 3), (21, 9), (24, 9)],
  [[(-1, 0), (8, 0.5), (14, 7.5), (24, 9)], [(-1, 15), (14, 15), (19, 11), (24, 9)]]),
 ("Frost Fen", 25, 17,
  [(-1, 2), (10, 2), (10, 6), (5, 6), (5, 11), (15, 11), (15, 4), (20, 4), (20, 14), (25, 14)],
  [[(-1, 8.5), (8, 8.5), (17, 1), (25, 14)], [(-1, 16), (12, 16), (18, 9), (25, 14)]]),
 ("Snowdrift Hollow", 27, 18,
  [(-1, 3), (8, 3), (8, 7), (3, 7), (3, 12), (13, 12), (13, 5), (18, 5), (18, 9), (15, 9),
   (15, 15), (23, 15), (23, 8), (27, 8)],
  [[(-1, 0), (10, 0.5), (20, 2), (27, 8)], [(-1, 17), (10, 17), (20, 11.5), (27, 8)],
   [(-1, 9.5), (9, 9.5), (17, 13), (27, 8)]]),
 ("Evergreen Summit", 30, 20,
  [(-1, 2), (12, 2), (12, 6), (4, 6), (4, 11), (16, 11), (16, 4), (22, 4), (22, 9), (19, 9),
   (19, 16), (27, 16), (27, 10), (30, 10)],
  [[(-1, 0), (14, 0), (25, 6), (30, 10)], [(-1, 19), (14, 19), (24, 13), (30, 10)],
   [(-1, 8.5), (10, 8.5), (18, 14), (30, 10)]]),
]

def segs_tiles(path, cols, rows):
    """Tiles of each segment (in-grid only), and problems found."""
    out, errs = [], []
    for i, ((c0, r0), (c1, r1)) in enumerate(zip(path, path[1:])):
        if c0 != c1 and r0 != r1: errs.append(f"segment {i} not straight")
        n = max(abs(c1 - c0), abs(r1 - r0))
        dc, dr = (c1 > c0) - (c1 < c0), (r1 > r0) - (r1 < r0)
        ts = [(c0 + dc * s, r0 + dr * s) for s in range(n + 1)]
        out.append([t for t in ts if 0 <= t[0] < cols and 0 <= t[1] < rows])
        for t in ts:
            if not (-1 <= t[0] <= cols and 0 <= t[1] < rows): errs.append(f"segment {i} leaves grid at {t}")
    return out, errs

def check(name, cols, rows, path, lanes):
    errs = []
    if path[0][0] != -1: errs.append("path must start at col -1")
    if path[-1][0] != cols: errs.append(f"path must end at col {cols}")
    segs, e = segs_tiles(path, cols, rows)
    errs += e
    owner = {}
    for i, ts in enumerate(segs):
        for t in ts:
            if t in owner and owner[t] not in (i - 1, i):
                errs.append(f"self-overlap at {t}")
            owner.setdefault(t, i)
    # Touching: tiles of non-adjacent segments must be >= 2 apart (Chebyshev).
    for i in range(len(segs)):
        for j in range(i + 2, len(segs)):
            for a in segs[i]:
                for b in segs[j]:
                    if max(abs(a[0] - b[0]), abs(a[1] - b[1])) < 2:
                        errs.append(f"segments {i} and {j} touch at {a}/{b}")
                        break
                else: continue
                break
    tiles = set(owner)
    backs = [abs(c1 - c0) for (c0, r0), (c1, r1) in zip(path, path[1:]) if c1 < c0]
    lane_info = []
    for k, lane in enumerate(lanes):
        if lane[0][0] != -1 or lane[-1][0] != cols: errs.append(f"lane {k} must run x=-1..{cols}")
        crossings, inside = 0, False
        for (x0, y0), (x1, y1) in zip(lane, lane[1:]):
            L = math.hypot(x1 - x0, y1 - y0)
            for s in range(int(L * 20)):
                x, y = x0 + (x1 - x0) * s / (L * 20), y0 + (y1 - y0) * s / (L * 20)
                if not (-0.5 <= y < rows - 0.5): errs.append(f"lane {k} leaves grid at y={y:.1f}"); break
                if not (0 <= x < cols - 0.5): continue
                on = (round(x), round(y)) in tiles
                crossings += on and not inside
                inside = on
        lane_info.append(crossings)
        if crossings < 1: errs.append(f"lane {k} never crosses the path")
    print(f"{name:17s} {cols}x{rows} path tiles {len(tiles):3d}, backtracks {backs or '-'}, "
          f"lane crossings {lane_info}  {'OK' if not errs else 'ERRORS: ' + '; '.join(sorted(set(errs)))}")
    return not errs

if __name__ == "__main__":
    ok = all([check(*m) for m in MAPS])
    print("ALL OK" if ok else "FIX NEEDED")
