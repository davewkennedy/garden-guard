#!/usr/bin/env bash
# Renders the app icons (icon-180.png, icon-192.png, icon-512.png) with headless chromium.
# Usage (WSL, any folder): bash tools/make-icons.sh
# 180 = apple-touch-icon: full opaque square (iOS rounds the corners itself).
# 192/512 = manifest icons: rounded square on a transparent background.
set -e
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
make() {   # size radius-percent out
  local S=$1 R=$2 OUT=$3
  cat > "$TMP/icon.html" <<EOF
<!doctype html><html><head><meta charset="utf-8"><style>
html,body{margin:0;width:${S}px;height:${S}px;background:transparent;overflow:hidden}
.sq{width:${S}px;height:${S}px;border-radius:${R}%;box-sizing:border-box;
  background:radial-gradient(circle at 50% 38%,#9be07a 0%,#5cb85c 55%,#2f7d3a 100%);
  display:flex;align-items:center;justify-content:center;position:relative}
.sun{font-size:${S}px;transform:scale(.62);line-height:1;
  font-family:"Noto Color Emoji","Apple Color Emoji","Segoe UI Emoji",sans-serif;
  filter:drop-shadow(0 ${S}px 0 rgba(0,0,0,0)) drop-shadow(0 calc(${S}px*.02) calc(${S}px*.03) rgba(0,0,0,.35))}
.plus{position:absolute;right:8%;top:6%;font:900 calc(${S}px*.17)/1 "Arial Black",Arial,sans-serif;
  color:#fff;text-shadow:0 calc(${S}px*.012) calc(${S}px*.02) rgba(0,0,0,.45)}
</style></head><body><div class="sq"><span class="sun">🌻</span><span class="plus">++</span></div></body></html>
EOF
  chromium --headless --disable-gpu --no-sandbox --hide-scrollbars --default-background-color=00000000 \
    --force-device-scale-factor=1 --window-size=${S},${S} --screenshot="$TMP/out.png" "file://$TMP/icon.html" \
    > /dev/null 2>&1
  mv "$TMP/out.png" "$ROOT/$OUT"
  echo "wrote $OUT"
}
make 180 0 icon-180.png
make 192 22 icon-192.png
make 512 22 icon-512.png
