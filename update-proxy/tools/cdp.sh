#!/bin/bash
# cdp.sh — Control the Chro viewport via Chrome DevTools Protocol
#
# Commands:
#   ./tools/cdp.sh reload              — Reload the viewport page
#   ./tools/cdp.sh screenshot [file]   — Take a PNG screenshot
#   ./tools/cdp.sh eval "JS code"      — Execute JavaScript in the page
#   ./tools/cdp.sh url <url>           — Navigate to a URL
#   ./tools/cdp.sh tabs                — List open tabs
#
# Requires: websocat (brew install websocat)
# CDP port default: 9222

CDP_PORT="${CDP_PORT:-9222}"
CDP_HOST="localhost"
CMD="${1:-tabs}"
shift

get_ws_url() {
  curl -s "http://${CDP_HOST}:${CDP_PORT}/json" | python3 -c "
import json, sys
tabs = json.load(sys.stdin)
for t in tabs:
    if t.get('type') == 'page':
        print(t['webSocketDebuggerUrl'])
        break
" 2>/dev/null
}

send_cdp() {
  local ws_url method params id msg
  ws_url="$(get_ws_url)"
  if [ -z "$ws_url" ]; then
    echo "Error: No CDP target found. Is Chro viewport running?"
    exit 1
  fi
  method="$1"
  params="${2:-\{\}}"
  id=$((RANDOM % 10000))
  msg=$(python3 -c "import json; print(json.dumps({'id':${id},'method':'${method}','params':json.loads('''${params}''')}))")
  printf '%s' "$msg" | websocat -n1 "$ws_url" 2>/dev/null
}

case "$CMD" in
  tabs)
    curl -s "http://${CDP_HOST}:${CDP_PORT}/json" | python3 -c "
import json, sys
tabs = json.load(sys.stdin)
for t in tabs:
    print(f'{t[\"id\"]:>4}  {t[\"type\"]:8}  {t[\"url\"]}')" 2>/dev/null
    ;;

  reload)
    send_cdp "Page.reload" '{"ignoreCache":true}' > /dev/null
    echo "Reloaded."
    ;;

  screenshot)
    OUTFILE="${1:-/tmp/viewport-screenshot.png}"
    RESULT="$(send_cdp "Page.captureScreenshot" '{"format":"png"}')"
    echo "$RESULT" | python3 -c "
import json, sys, base64
data = json.load(sys.stdin)
if 'result' in data and 'data' in data['result']:
    raw = base64.b64decode(data['result']['data'])
    with open('${OUTFILE}', 'wb') as f:
        f.write(raw)
    print(f'Screenshot saved: ${OUTFILE} ({len(raw)} bytes)')
else:
    print('Error:', json.dumps(data), file=sys.stderr)
    sys.exit(1)
" 2>/dev/null
    ;;

  eval)
    CODE="$1"
    if [ -z "$CODE" ]; then
      echo "Usage: cdp.sh eval \"document.title\""
      exit 1
    fi
    ESCAPED="$(echo "$CODE" | python3 -c "import json,sys; print(json.dumps(sys.stdin.read().strip()))")"
    RESULT="$(send_cdp "Runtime.evaluate" "{\"expression\":${ESCAPED},\"returnByValue\":true}")"
    echo "$RESULT" | python3 -c "
import json, sys
data = json.load(sys.stdin)
r = data.get('result', {}).get('result', {})
if 'value' in r:
    print(r['value'])
elif 'description' in r:
    print(r['description'])
else:
    print(json.dumps(data, indent=2))
" 2>/dev/null
    ;;

  url)
    URL="$1"
    if [ -z "$URL" ]; then
      echo "Usage: cdp.sh url <url>"
      exit 1
    fi
    send_cdp "Page.navigate" "{\"url\":\"${URL}\"}" > /dev/null
    echo "Navigated to: ${URL}"
    ;;

  *)
    echo "Unknown command: $CMD"
    echo "Usage: cdp.sh {tabs|reload|screenshot [file]|eval \"js\"|url <url>}"
    exit 1
    ;;
esac
