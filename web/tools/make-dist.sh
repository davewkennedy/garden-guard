#!/bin/bash
# Builds a clean copy of the game for uploading to a web host:
#   dist/gardenguard-web/      (the files to upload: index.html, css/, js/, icons, manifest, sw.js)
#   dist/gardenguard-web.zip   (the same, zipped)
# Leaves out notes/, tools/, shots/ and dist/ itself. Run it from anywhere:  bash tools/make-dist.sh
set -e
cd "$(dirname "$0")/.."
OUT=dist/gardenguard-web
rm -rf dist
mkdir -p "$OUT"
cp -r index.html manifest.json sw.js css js "$OUT"/
[ -d audio ] && cp -r audio "$OUT"/     # music (loaded by js/music.js; also listed in sw.js for offline play)
cp icon-*.png "$OUT"/
[ -f README.txt ] && cp README.txt "$OUT"/
python3 - "$OUT" <<'EOF'
import os, sys, zipfile
root = sys.argv[1]
base = os.path.dirname(root)
with zipfile.ZipFile(os.path.join(base, 'gardenguard-web.zip'), 'w', zipfile.ZIP_DEFLATED) as z:
    for d, _, files in os.walk(root):
        for f in files:
            p = os.path.join(d, f)
            z.write(p, os.path.relpath(p, base))
EOF
echo "files:"; (cd "$OUT" && find . -type f | sort)
ls -la dist/gardenguard-web.zip
