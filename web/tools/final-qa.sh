#!/usr/bin/env bash
# Garden Guard++ web: final QA on the upload copy (owner: Dale). Run in WSL from anywhere.
#   tools/final-qa.sh [PAGE]    default dist/gardenguard-web/index.html
# Prints one line per run and a final PASS/FAIL line; full logs in shots/qa-*.log.
cd "$(dirname "$0")/.." || exit 2
PAGE="${1:-dist/gardenguard-web/index.html}"
fails=0
run() {   # name, args...
  local name="$1"; shift
  if timeout 400 node "$@" > "shots/qa-$name.log" 2>&1; then echo "PASS $name"
  else echo "FAIL $name  ($(grep -E '\[FAIL\]|uncaught|console.error' "shots/qa-$name.log" | head -3 | tr -s ' ' | tr '\n' ';'))"; fails=$((fails+1)); fi
}
date +%T
run pc            tools/browser-run.js --page "$PAGE" --seconds 3 --shot shots/qa-pc.png
for s in iphone android; do for m in 1 2 8; do
  run "$s-map$m" tools/browser-run.js --page "$PAGE" --size $s --query "map=$m" --touch
done; done
run iphone-portrait tools/browser-run.js --page "$PAGE" --size iphone-portrait --rotate yes --shot shots/qa-iphone-portrait.png
run ipad-portrait   tools/browser-run.js --page "$PAGE" --size ipad-portrait --rotate no --touch --shot shots/qa-ipad-portrait.png
run ipad            tools/browser-run.js --page "$PAGE" --size ipad --rotate no --touch
run se              tools/browser-run.js --page "$PAGE" --size se --rotate no --touch
run e2e-iphone      tools/e2e-touch.js --page "$PAGE" --size iphone --strict
run e2e-se          tools/e2e-touch.js --page "$PAGE" --size se --strict
run auto-speed3     tools/browser-run.js --page "$PAGE" --query "autopilot=1&speed=3" --seconds 60 --shot shots/qa-auto.png
node tools/parity.js > shots/qa-parity.log 2>&1 && echo "PASS parity (node, exact C++)" || { echo "FAIL parity"; fails=$((fails+1)); }
date +%T
[ $fails -eq 0 ] && echo "QA PASS" || echo "QA FAIL ($fails)"
