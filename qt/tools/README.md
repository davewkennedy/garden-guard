# Balance tools

Campaign-tuning scripts by claude2-de, moved here from a scratchpad. Run them from the project root. They find the project from their own location, or from `GG_ROOT` if it's set.

| File | What it does |
|---|---|
| `maps10.py` | The 10 map layouts (grid, path, sky lanes) and a validator. It checks straight segments, no touching or overlapping path, and that lanes cross the path. Imported by `gen10.py`. |
| `gen10.py` | Generates the whole `MAPS` block of `src/data.h` from its tables: per-map waves (maps 1-5 hand-tuned, 6-10 from a template), terrain, snow, path2, wind, and per-map HP scale and start coins. |
| `tune.sh` | Balance sweep. For each variant of `gen10.py` arguments, it regenerates `MAPS`, rebuilds `garden_simtest` and prints the autopilot campaign as one line. |

## ⚠️ Both write to `src/data.h`

`gen10.py` without `--dry` **overwrites the MAPS block** in `$GG_ROOT/src/data.h`. `tune.sh` always does, and **the last variant stays applied**. To experiment safely, point `GG_ROOT` at a copy of the project, or revert `data.h` afterwards.

Its built-in start coins are older than the shipped ones. **Always pass the final tuning** (below), or you'll silently revert the balance.

## Examples

```sh
# Validate the layouts (read-only)
python3 tools/maps10.py

# Preview the generated MAPS and per-map bug usage (read-only)
python3 tools/gen10.py --dry --season --report

# Final shipped tuning, as M=hpScale,startCoins for maps 2-10 (map 1 keeps its defaults):
FINAL="2=0.9,360 3=0.95,420 4=1.15,500 5=1.15,500 6=1.15,600 7=1.2,660 8=1.0,740 9=0.5,1100 10=1.15,960"

# Sweep on a scratch copy, so the real data.h isn't touched
mkdir -p ~/tmp/gg && cp -r CMakeLists.txt src tests tools ~/tmp/gg/
GG_ROOT=~/tmp/gg tools/tune.sh "$FINAL" "$(echo $FINAL | sed 's/7=1.2,660/7=1.3,660/')"
# [2=0.9,360 ... 10=1.15,960] => m1C5:20 m2C5:20 m3C5:20 m4C5:18 m5C5:18 m6C5:16 m7C5:14 m8C5:14 m9C5:12 m10W5:6
```

Result format: `m<map><C=cleared|W=won|L=lost><wave>:<lives left>`.

Environment knobs for `gen10.py`:
- `CLIMAX_N` (default 1.3): swarm size of waves 4-5 on maps 6-10.
- `CLIMAX_HP` (default 1.1): HP of waves 4-5 on maps 6-10.
- `OPEN_N` (default 1.0): swarm size of waves 1-2 on maps 9-10.

For `tune.sh`: `BUILD` sets the build directory (default `build/tune`, configured automatically).

## Caveats

- If `MapDef` gains new trailing fields (for example difficulty modes), update the `trail` list in `gen10.py`'s `emit()` to match the field order.
- Notes on the balance cliffs these tools found are in `notes/balance.md`.
