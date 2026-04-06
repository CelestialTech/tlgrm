---
name: viewport
description: Develop and iterate on isometric 3D voxel objects using Chro browser as a live viewport
user_invocable: true
---

# Isometric 3D Object Development

Develop eBoy-style isometric voxel objects in Three.js using Chro (custom Chromium) as a live viewport. Edit geometry, reload instantly via CDP, screenshot for feedback, iterate.

**Stack:** Chro browser + Chrome DevTools Protocol + websocat. No Puppeteer, no Node.js.

## Architecture

| Component | Path |
|-----------|------|
| Chro binary | `/Volumes/4TBNVMe/src/out/Chro/Chro.app/Contents/MacOS/Chromium` |
| Launcher | `tools/viewport.sh` |
| CDP controller | `tools/cdp.sh` |
| Preview scene | `tools/preview.html` |
| Three.js vendor | `site/js/vendor/three.module.min.js` |
| CDP port | `9222` |
| User data dir | `/tmp/chro-viewport` (isolated profile) |
| Screenshot output | `/tmp/viewport-screenshot.png` |

**No Puppeteer. No Node.js.** Just Chro + bash + websocat over CDP.

## Prerequisites

- Chro built at `/Volumes/4TBNVMe/src/out/Chro/Chro.app`
- `websocat` installed (`brew install websocat`)

## Launching

```bash
cd /Users/pasha/xCode/tlgrm/update-proxy
./tools/viewport.sh           # default 1200x900
./tools/viewport.sh 1600 1200 # custom size
```

The script launches Chro with these critical flags:
- `--app=file://...` — chromeless window (no tabs, no address bar)
- `--remote-debugging-port=9222` — CDP control endpoint
- `--allow-file-access-from-files` — required for ES module imports from `file://` (CORS blocks them otherwise)
- `--user-data-dir=/tmp/chro-viewport` — isolated profile, won't interfere with normal browsing

## CDP Commands (tools/cdp.sh)

```bash
./tools/cdp.sh tabs                          # List open tabs
./tools/cdp.sh reload                        # Reload page (cache-busting)
./tools/cdp.sh screenshot [output.png]       # Capture PNG (default: /tmp/viewport-screenshot.png)
./tools/cdp.sh eval "JS expression"          # Execute JavaScript, print result
./tools/cdp.sh url "file:///path/to.html"    # Navigate to different page
```

Override CDP port: `CDP_PORT=9333 ./tools/cdp.sh reload`

## Design Iteration Workflow

When iterating on 3D objects with the user:

1. **Check** if viewport is running: `pgrep -f "remote-debugging-port=9222"` or `curl -s http://localhost:9222/json`
2. **Launch** if not running: `./tools/viewport.sh &` (background it, wait ~3s for CDP to come up)
3. **Edit** `tools/preview.html` — modify the Three.js scene between the `OBJECT UNDER REVIEW` markers
4. **Reload**: `./tools/cdp.sh reload`
5. **Screenshot**: `./tools/cdp.sh screenshot /tmp/viewport-screenshot.png`
6. **Show** screenshot to user using the Read tool on the PNG file
7. **Get feedback**, repeat from step 3

## Preview HTML Structure

`tools/preview.html` is a self-contained Three.js scene:

- **Import map** resolves `three` → `../site/js/vendor/three.module.min.js`
- **Renderer**: WebGLRenderer, antialias, PCFSoftShadowMap, clear color `0x333344`
- **Camera**: OrthographicCamera, `d=20`, position `(50,50,50)`, `lookAt(0,0,0)` — produces 2:1 dimetric isometric angle
- **Lights**: ambient `0x6a6a8a` at 0.6 + directional white at 1.2 from `(10,20,10)` with shadow
- **Ground grid**: 20x20 InstancedMesh of 0.9-unit tiles, color `0x1a3a4a`
- **Test object**: cyan emissive cube at origin (replace with actual object)
- **Turntable**: rotates at 0.03 rad/frame, pauses 1.5s at each of 4 isometric faces (0, 90, 180, 270 degrees)
- **Render loop**: continuous `requestAnimationFrame` loop
- **Exposed globals**: `window.__scene`, `window.__camera`, `window.__renderer`, `window.__rendered`

### Object Placement Zone

Edit between these markers:
```js
// ─── OBJECT UNDER REVIEW ────────────────────────────────────
// ... Three.js geometry here — centered at origin, Y-up ...
// ─── END OBJECT ──────────────────────────────────────────────
```

## Turntable Controls

The scene auto-rotates around Y, pausing 1.5s at each of the 4 cardinal isometric views. Constants in `preview.html`:

| Constant | Default | Purpose |
|----------|---------|---------|
| `PAUSE_MS` | 1500 | Hold time at each face (ms) |
| `SPIN_SPEED` | 0.03 | Rotation speed between stops (rad/frame) |
| `SNAP_THRESHOLD` | 0.04 | Proximity to snap to a stop (rad) |

## Runtime Debugging via CDP

```bash
# Scene child count
./tools/cdp.sh eval "window.__scene.children.length"

# Check canvas dimensions
./tools/cdp.sh eval "window.innerWidth + 'x' + window.innerHeight"

# Confirm WebGL context
./tools/cdp.sh eval "document.querySelector('canvas').getContext('webgl2') ? 'webgl2' : 'none'"

# Adjust camera zoom at runtime
./tools/cdp.sh eval "window.__camera.left=-30; window.__camera.right=30; window.__camera.top=22; window.__camera.bottom=-22; window.__camera.updateProjectionMatrix(); 'zoomed out'"
```

## Troubleshooting

| Problem | Cause | Fix |
|---------|-------|-----|
| Screenshot is solid dark color | Camera not pointing at objects | Ensure `camera.lookAt(0,0,0)` is called after setting position |
| Three.js module fails to load | CORS blocks `file://` imports | Must launch with `--allow-file-access-from-files` |
| `cdp.sh` says "No CDP target" | Viewport not running or port conflict | Check `pgrep -f remote-debugging-port`, kill stale processes |
| Scene renders in browser but black via CDP screenshot | Single-frame render without rAF loop | Use `requestAnimationFrame` render loop, not single `renderer.render()` |
| `websocat` not found | Not installed | `brew install websocat` |

## Killing the Viewport

```bash
pkill -f "remote-debugging-port=9222"
```
