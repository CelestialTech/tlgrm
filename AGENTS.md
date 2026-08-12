# Tlgrm — repository guide

Read this before doing anything in this repository. `tdesktop/AGENTS.md` is
upstream Telegram Desktop's guide and describes a cross-platform project; this
file describes **ours**, and where the two disagree, this one wins.

Tlgrm is a **macOS-only** fork of Telegram Desktop with an embedded MCP server.
Current version **7.0.9** (`AppVersion 7000009`), built on upstream `v7.0.9`.

---

## The tooling already exists. Find it before writing anything.

This file exists because an assistant ran `ls *.sh` at the root, concluded "no
release script exists", and started hand-rolling steps — while
`tools/publish_update.py`, added by a commit literally titled *"add the release
publishing pipeline"*, sat in the tree. Before concluding something is missing:

```bash
find . -name '*.sh' -o -name '*.py' | grep -v ThirdParty | grep -v Libraries
grep -rl '<what it would do>' --include='*.sh' --include='*.py' .
git log --all --oneline --diff-filter=AD --name-only | grep -i <concept>
```

If an artifact of that kind already exists in the tree, something produced it.

---

## Cutting a release

1. **Build universal** — a plain `-scheme` build silently emits host-arch only:
   ```bash
   cd tdesktop/Telegram && ./configure.sh -D TDESKTOP_API_ID=2040 -D TDESKTOP_API_HASH=b18441a1ff607e10a989891a5462e627
   cd ../out && xcodebuild -project Telegram.xcodeproj -scheme Telegram \
     -configuration Release -destination 'generic/platform=macOS' build -jobs 24
   lipo -info Release/Tlgrm.app/Contents/MacOS/Tlgrm   # must say: x86_64 arm64
   ```
2. **Fix the icon** (manual, every build):
   ```bash
   iconutil --convert icns --output out/Release/Tlgrm.app/Contents/Resources/AppIcon.icns \
     Telegram/Telegram/Images.xcassets/Icon.iconset/
   ```
3. **DMG** — `./create_dmg.sh` → `dmg_build/Tlgrm_<version>.dmg`.
   `create_beautiful_dmg.sh` is the fuller variant; see the warning below.
4. **Update packages** — `uv run tools/publish_update.py --version 7000009`.
   It strips + re-signs the bundle, packs both platform keys, copies to the
   HTTP origin, and posts to the MTProto channel.
5. **GitHub release** (still manual — no automation exists):
   ```bash
   gh release create v7.0.9 --repo CelestialTech/tlgrm \
     --title "Tlgrm v7.0.9" --notes-file RELEASE_NOTES_7.0.9.md \
     dmg_build/Tlgrm_7.0.9.dmg
   ```

### Traps that cost hours

- **`xcodebuild build` never strips.** The unstripped universal binary is
  1.56 GB; Packer rejects anything over 1 GB with `Bad result len`, which reads
  like a compression fault rather than "too big". `publish_update.py` strips and
  re-signs automatically (`--no-strip` opts out).
- **Both platform keys must be published.** `armac` (Apple Silicon) and `mac`
  (Intel). The binary is universal; Packer's `-arch` only picks the output
  *filename*, and the two packages are byte-identical.
- **The feed JSON must be the update channel's LAST message** — `MtpChecker`
  reads history with `limit=1`. Post anything after it and update checks break.
- **`create_beautiful_dmg.sh` can embed a logged-in session.** It packages
  `~/tdata.zip` into `initiate.pkg`, and `postinstall` copies it into the
  installing user's home. It now requires `INCLUDE_TDATA=1`; a DMG built that
  way must never be published.

---

## Layout

| Path | What it is |
|---|---|
| `tdesktop/` | The fork (submodule). Branch `upgrade-v<version>`. |
| `tools/publish_update.py` | **The release publishing pipeline.** |
| `tools/mcp_*.py` | MCP test suites (sweep, smoke, fixtures). |
| `create_dmg.sh` | Canonical DMG build. |
| `update-server/` | Rust HTTP origin, deployed to ironforge.local. |
| `cloudflare-worker/` | **Superseded** — no longer serves updates. |
| `DesktopPrivate/` | RSA update-signing keys. Never commit, print, or upload. |
| `docs/UPDATE_SYSTEM.md` | How updates work, both paths. |

**Repo warning:** one GitHub repo (`CelestialTech/tlgrm`) backs both the parent
repo and the `tdesktop` submodule, and `origin/master` there belongs to the
**parent**. Never push the submodule's master to it — the submodule publishes
as `upgrade-v<version>` branches.

---

## Standing rules

- macOS only. Do not document or "fix" Windows/Linux paths.
- `ENABLE_APP_SANDBOX = NO` on every target.
- No AI attribution in commit messages, ever.
- Use `uv` for Python, never raw `python3`/`pip`.
- MCP: 355 tools, four declaration sites must agree — see
  `tdesktop/Telegram/SourceFiles/mcp/mcp_tool_backing.h` for the backing table.
