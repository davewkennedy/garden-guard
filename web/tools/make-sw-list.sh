#!/usr/bin/env bash
# Regenerates the file list and CACHE version in sw.js (owner: Bill).
# - FILES = every local <script src> and <link href> in index.html, plus index.html itself, the manifest's
#   icons and anything else linked (apple-touch-icon etc.).
# - CACHE = 'ggweb-<hash of those files' contents>', so any change to the game makes phones update.
# Run it before hosting a new version:  bash tools/make-sw-list.sh
set -e
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"
python3 - <<'EOF'
import hashlib, json, os, re
html = open('index.html', encoding='utf-8').read()
refs = re.findall(r'<(?:script[^>]*\bsrc|link[^>]*\bhref)\s*=\s*["\']([^"\'#?]+)', html)
files = ['./', 'index.html']
for r in refs:
    if re.match(r'^[a-z]+:|^//', r): continue          # external URLs aren't cached
    files.append(r.lstrip('./') if r.startswith('./') else r)
try:
    for icon in json.load(open('manifest.json', encoding='utf-8')).get('icons', []):
        files.append(icon['src'])
except Exception:
    pass
seen, out = set(), []
for f in files:
    if f in seen: continue
    seen.add(f)
    if f != './' and not os.path.isfile(f):
        print('warning: missing file, skipped:', f); continue
    out.append(f)
h = hashlib.sha1()
for f in out:
    if f != './': h.update(f.encode()); h.update(open(f, 'rb').read())
version = 'ggweb-' + h.hexdigest()[:10]
sw = open('sw.js', encoding='utf-8').read()
lines = ',\n'.join('  ' + json.dumps(f) for f in out)
sw = re.sub(r"const CACHE = '[^']*';", f"const CACHE = '{version}';", sw)
sw = re.sub(r'const FILES = \[.*?\];', 'const FILES = [\n' + lines + ',\n];', sw, flags=re.S)
open('sw.js', 'w', encoding='utf-8', newline='\n').write(sw)
print(f'sw.js: {len(out)} files, CACHE = {version}')
EOF
