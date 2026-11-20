#!/usr/bin/env bash
# Garden Guard++ web: headless chromium screenshots (owner: Dale). Run in WSL.
#
#   tools/shot.sh NAME [QUERY] [SIZES] [BUDGET_MS]
#     NAME    file prefix: writes shots/NAME-pc.png, shots/NAME-iphone.png, shots/NAME-android.png
#     QUERY   URL query without '?', e.g. "scene=play&map=3"           (default: none)
#     SIZES   comma list of pc,iphone,android                          (default: all three)
#     BUDGET  --virtual-time-budget in ms (lets the game run)          (default: 3000)
#   Examples:
#     tools/shot.sh play3 "scene=play&map=3"
#     tools/shot.sh auto "autopilot=1&speed=4" pc 8000
#   Console errors/warnings from the page are printed after each shot (and saved to
#   shots/NAME-SIZE.log). Exit code 1 if any page logged an error.
set -u
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NAME="${1:?usage: tools/shot.sh NAME [QUERY] [SIZES] [BUDGET_MS]}"
QUERY="${2:-}"
SIZES="${3:-pc,iphone,android}"
BUDGET="${4:-3000}"
BIN="$(command -v chromium || command -v chromium-browser || command -v google-chrome)"
[ -n "$BIN" ] || { echo "shot.sh: chromium not found" >&2; exit 2; }
mkdir -p "$DIR/shots"
URL="file://$DIR/index.html${QUERY:+?$QUERY}"
status=0
IFS=',' read -ra LIST <<< "$SIZES"
for s in "${LIST[@]}"; do
  case "$s" in
    pc)      W=1280; H=800 ;;
    iphone)  W=844;  H=390 ;;
    android) W=915;  H=412 ;;
    *) echo "shot.sh: unknown size $s (pc, iphone, android)" >&2; continue ;;
  esac
  OUT="$DIR/shots/$NAME-$s.png"
  LOG="$DIR/shots/$NAME-$s.log"
  # Phones: touch + mobile UA-ish flags so pointer/touch code paths and media queries apply.
  EXTRA=()
  if [ "$s" != pc ]; then EXTRA=(--touch-events=enabled --enable-features=OverlayScrollbar); fi
  timeout 60 "$BIN" --headless --disable-gpu --no-sandbox --hide-scrollbars --mute-audio \
    --allow-file-access-from-files --window-size=$W,$H --virtual-time-budget="$BUDGET" \
    --enable-logging=stderr --v=0 "${EXTRA[@]}" --screenshot="$OUT" "$URL" > "$LOG" 2>&1
  # Keep only the page's console lines (chromium prints them as CONSOLE / "source: file://").
  CONSOLE="$(grep -E 'CONSOLE|Uncaught|source: file://' "$LOG" | sed -E 's/^\[[^]]*\] //')"
  if [ -f "$OUT" ]; then echo "shot: $OUT (${W}x${H})"; else echo "shot: FAILED $OUT (see $LOG)"; status=1; fi
  if [ -n "$CONSOLE" ]; then
    echo "$CONSOLE" | head -20 | sed 's/^/    console: /'
    if echo "$CONSOLE" | grep -qiE 'error|uncaught'; then status=1; fi
  fi
done
exit $status
