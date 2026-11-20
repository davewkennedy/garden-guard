#!/usr/bin/env bash
# Balance sweep (from claude2-de). For each variant: regenerate MAPS with gen10.py, rebuild
# garden_simtest, run it, and print the autopilot campaign (Test 3) as one line per variant:
#   m<map><C=cleared|L=lost|W=won><wave>:<lives> ...
# usage: tools/tune.sh "<gen10 args>" ["<gen10 args>" ...]
# WARNING: rewrites src/data.h, and the LAST variant stays applied. Run it on a copy
# (GG_ROOT=/path/to/copy) or revert data.h afterwards.
# Env: GG_ROOT = project root (default: the folder above tools/); BUILD = build dir (default build/tune).
T=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
export GG_ROOT=${GG_ROOT:-$(dirname "$T")}
BUILD=${BUILD:-build/tune}
cd "$GG_ROOT" || exit 1
[ -f "$BUILD/build.ninja" ] || cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Debug > /dev/null
grep -q "enum class Season" src/data.h && SEASON=--season || SEASON=
for v in "$@"; do
  python3 "$T/gen10.py" $SEASON $v > /dev/null || { echo "gen failed: $v"; continue; }
  cmake --build "$BUILD" --target garden_simtest 2>&1 | grep -E "error" | head -3
  r=$("./$BUILD/garden_simtest" | sed -n '/Test 3/,$p' | grep -E "at wave" | sed -E 's/^\s+map ([0-9]+) .*-> ([A-Za-z])[A-Za-z]* at wave ([0-9]+), lives ([0-9]+).*/m\1\U\2\E\3:\4/' | tr '\n' ' ')
  echo "[$v] => $r"
done
date +%T
