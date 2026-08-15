# Tlgrm — repository guide

Read this before doing anything in this repository. `tdesktop/AGENTS.md` is
upstream Telegram Desktop's guide and describes a cross-platform project; this
file describes **ours**, and where the two disagree, this one wins.

Tlgrm is a **macOS-only** fork of Telegram Desktop with an embedded MCP server.
Current version **7.0.10** (`AppVersion 7000010`), built on upstream `v7.0.9`.
The fork's patch number advances independently of upstream once a
release ships without an upstream bump — the updater compares the
integer, so it has to increase for an update to be offered at all.

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

Order matters: everything downstream must contain the **signed** bundle, so
signing happens before the DMG and before the update packages.

1. **Build universal** — a plain `-scheme` build silently emits host-arch only:
   ```bash
   cd tdesktop/Telegram && ./configure.sh -D TDESKTOP_API_ID=2040 -D TDESKTOP_API_HASH=b18441a1ff607e10a989891a5462e627
   cd ../out && xcodebuild -project Telegram.xcodeproj -scheme Telegram \
     -configuration Release -destination 'generic/platform=macOS' build -jobs 24
   lipo -info Release/Tlgrm.app/Contents/MacOS/Tlgrm   # must say: x86_64 arm64
   ```
2. **Strip** — `xcodebuild build` never strips, and Packer refuses anything over
   1 GB uncompressed. Strip by hand so the signature in step 3 is applied to the
   final bytes:
   ```bash
   strip -x out/Release/Tlgrm.app/Contents/MacOS/Tlgrm   # 1.56 GB -> ~491 MB
   ```
3. **Sign with the Developer ID — NOT optional.** An ad-hoc build runs fine from
   the build directory and Gatekeeper refuses it the moment anyone downloads it,
   because the download is quarantined and ad-hoc carries no usable signature.
   `get-task-allow` must come out first; notarization rejects it.
   ```bash
   codesign -d --entitlements :- out/Release/Tlgrm.app > /tmp/ents.plist
   /usr/libexec/PlistBuddy -c "Delete :com.apple.security.get-task-allow" /tmp/ents.plist
   ~/.claude/skills/macos-codesign/sign.sh --app out/Release/Tlgrm.app --entitlements /tmp/ents.plist
   ```
4. **DMG, from the signed bundle** — `./create_dmg.sh` →
   `dmg_build/Tlgrm_<version>.dmg`. The disk image needs its **own** signature;
   a stapled ticket alone still assesses as "no usable signature".
   ```bash
   codesign --force --sign "Developer ID Application: Rodion Nazarov (LGAQBC2VM2)" \
     --timestamp dmg_build/Tlgrm_<version>.dmg
   ~/.claude/skills/macos-codesign/notarize.sh --target dmg_build/Tlgrm_<version>.dmg
   ```
5. **Verify with `spctl`, never `codesign --verify`** — the latter passes on an
   ad-hoc signature and tells you nothing about Gatekeeper:
   ```bash
   hdiutil attach dmg_build/Tlgrm_<version>.dmg
   spctl -a -vvv --type exec /Volumes/Tlgrm/Tlgrm.app  # accepted / Notarized Developer ID
   find /Volumes/Tlgrm -iname '*.pkg' -o -iname '*tdata*'   # must be empty
   ```
6. **Update packages, from the same signed bundle** —
   `uv run tools/publish_update.py --version <int> --no-strip`. Use `--no-strip`:
   stripping would force an ad-hoc re-sign and silently downgrade the signature
   (the script now refuses rather than doing it). It packs both platform keys,
   copies to the HTTP origin, and posts to the MTProto channel.
   `--testing` publishes under the feed's testing key instead, which only
   clients that typed `testupdate` will see.
7. **GitHub release** (still manual — no automation exists):
   ```bash
   gh release create v<version> --repo CelestialTech/tlgrm \
     --title "Tlgrm v<version>" --notes-file RELEASE_NOTES_<version>.md \
     dmg_build/Tlgrm_<version>.dmg
   ```
8. **Install it** — upgrade `/Applications/Tlgrm.app` itself. The updater
   replaces the bundle the client is *running from*, so testing against a copy
   elsewhere leaves the real installation untouched.

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
- MCP: 355 tools, 619 described parameters, four declaration sites must agree — see
  `tdesktop/Telegram/SourceFiles/mcp/mcp_tool_backing.h` for the backing table.
