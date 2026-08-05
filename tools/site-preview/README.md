# tools/site-preview

Browser tooling for authoring the 3D assets in `../../site`. **Development only** —
nothing here ships, and none of it is part of the desktop app or the update server.

This previously lived inside `update-proxy/`, where it read as foreign — a headless
Chromium harness sitting in a Rust server crate. It is neither. It is the harness
that produced the landing page's isometric objects.

## Contents

| File | What |
|---|---|
| `viewport.sh` / `viewport.mjs` | Opens a chrome-less window rendering `preview.html` with the Chrome DevTools Protocol enabled on **port 9222** (the Chro browser's CDP port). |
| `cdp.sh` | CDP helper — evaluate JS, capture screenshots, drive the page from the shell. Defaults to port 9222, override with `CDP_PORT`. |
| `preview.html` | The Three.js preview harness the viewport loads. |
| `render-object.mjs` | Renders a single object to an image for inspection. |
| `vault-sketch.html` | SVG reference sketches for the vault geometry. |

`package.json` here exists for `puppeteer`, used by the `.mjs` scripts. It is not
the website's manifest — the site has no build step (see `../../site/README.md`).

## Usage

```bash
npm install                 # once, for puppeteer
./viewport.sh               # launches the preview window with CDP on 9222
curl -s http://localhost:9222/json | jq '.[].title'
CDP_PORT=9222 ./cdp.sh screenshot out.png
```

## Related

- `../../site` — what this tooling is for
- `../../update-proxy` — the server that hosts the site
