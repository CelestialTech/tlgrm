#!/bin/bash
# viewport.sh — Launch Chro in chromeless viewport mode with CDP remote control
#
# Opens a chrome-less window rendering preview.html with CDP enabled on port 9222.
# No Puppeteer, no Node.js — just Chro + HTTP API.
#
# Usage:
#   ./tools/viewport.sh [width] [height]
#   ./tools/viewport.sh 1200 900
#
# CDP control (from another terminal or script):
#   # List open tabs
#   curl -s http://localhost:9222/json
#
#   # Reload the page
#   curl -s http://localhost:9222/json | python3 -c "
#     import json,sys; tabs=json.load(sys.stdin)
#     ws=tabs[0]['webSocketDebuggerUrl']; print(ws)
#   "
#   # Then send Page.reload via websocat/wscat to that WS URL
#
#   # Take a screenshot (base64 PNG via CDP)
#   See tools/viewport-screenshot.sh

CHRO="/Volumes/4TBNVMe/src/out/Chro/Chro.app/Contents/MacOS/Chromium"
PREVIEW="$(cd "$(dirname "$0")" && pwd)/preview.html"
DATA_DIR="/tmp/chro-viewport"
CDP_PORT=9222
WIDTH="${1:-1200}"
HEIGHT="${2:-900}"

if [ ! -f "$CHRO" ]; then
  echo "Error: Chro binary not found at $CHRO"
  exit 1
fi

if [ ! -f "$PREVIEW" ]; then
  echo "Error: preview.html not found at $PREVIEW"
  exit 1
fi

echo "Launching Chro viewport (${WIDTH}x${HEIGHT}), CDP on port ${CDP_PORT}..."

exec "$CHRO" \
  --app="file://${PREVIEW}" \
  --window-size="${WIDTH},${HEIGHT}" \
  --remote-debugging-port="${CDP_PORT}" \
  --user-data-dir="${DATA_DIR}" \
  --allow-file-access-from-files \
  --disable-extensions \
  --hide-scrollbars \
  --no-first-run \
  --no-default-browser-check \
  2>/dev/null
