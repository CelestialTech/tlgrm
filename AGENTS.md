# Tlgrm — repository guide

Read this before doing anything in this repository. `tdesktop/AGENTS.md` is
upstream Telegram Desktop's guide and describes a cross-platform project; this
file describes **ours**, and where the two disagree, this one wins.

Tlgrm is a **macOS-only** fork of Telegram Desktop with an embedded MCP server.
Current version **7.0.9a** (`AppVersion 700000901`), built on upstream `v7.0.9`.

## Versioning — several fork releases per upstream base

The fork ships more than one release on a single Telegram Desktop base, so the
version carries two numbers: upstream's, and ours.

```
display   7.0.9a            7.0.9b            7.0.12a
AppVersion 700000901        700000902         700001201
           = 7000009*100+1  = 7000009*100+2   = 7000012*100+1
```

`AppVersion = upstreamBase * 100 + index`, where index 1..99 is this fork's
release on that base and renders as a letter. Set it with the version script,
which understands the letter form directly:

```bash
cd tdesktop/Telegram && python build/set_version.py 7.0.9b
```

Why this shape:

- **The updater compares `AppVersion` with a strict `>`.** Every release we
  ship has to raise it, so a fork release cannot reuse upstream's number.
- **Monotonic on both axes.** A newer upstream base outranks every letter on an
  older one (`700001200 > 700000999`), so a rebase never collides with the
  letters already shipped.
- **The base stays readable** in the integer and in package filenames
  (`tarmacupd700000901`), which matters when reading a feed.
- **Bounds:** Packer refuses anything over `999999999` (`packer.cpp:207`) and
  tdata stores the value as `qint32`. The worst case in this scheme,
  `9.999.999z` → `999999926`, clears both. It stops working at upstream major
  10, which is when the scheme needs revisiting.

There are **three version fields and three consumers**, and they are not
interchangeable:

| Field | Read by | Value for 7.0.9a |
|---|---|---|
| `AppVersion` | the updater's comparison, tdata gates | `700000901` |
| `AppVersionStr` | About box, logs, what people read | `7.0.9a` |
| `AppVersionOriginal` | `cmake/version.cmake` parser **only** | `7.0.9` |

`cmake/version.cmake` is a shared upstream submodule, not ours to change. It
splits `AppVersionOriginal` on `.` and does integer math on the parts, so that
field must stay plain upstream numbering. The bundle's
`CFBundleShortVersionString` would then read "7.0.9" for every letter, so the
root `CMakeLists.txt` overrides the two display variables from
`AppVersionStr` — the plist reads `7.0.9a`, and the override lives in our file
rather than the submodule. `set_version.py` derives all three fields from one
argument, so use it rather than editing by hand; editing `build/version` alone
leaves a stale plist, silently, because that regex is unanchored and cmake
caches its parse.

Only two places decode the integer, both display-only —
`FormatVersionDisplay` / `FormatVersionPrecise` in `core/changelogs.cpp` — and
they handle legacy values too, since `oldVersion` may predate the scheme
(anything ≤ 7 digits is read as plain upstream numbering). Everything else,
including the tdata gates in `localstorage.cpp`, treats `AppVersion` as an
opaque monotonic integer.

**This is a one-way door:** tdata written by a higher version cannot be opened
by a lower one. Going from `7000009` to `700000901` is a normal upgrade, but it
cannot be walked back.

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

```bash
tools/release.py 7.0.9b            # the whole thing
tools/release.py 7.0.9b --dry-run  # print the plan, touch nothing
```

One command owns the order: version → build → verify universal → strip → sign
→ DMG → notarize → verify with `spctl` → pack → publish both paths → GitHub
release. It calls the same underlying tools that were always there; what it
adds is knowing they are **ordered**, and refusing to continue when a step's
output is not what the next step needs.

Steps are idempotent rather than resumable — each skips when its output already
exists for the target version — so re-running after a failure is cheap.

`RELEASE_NOTES_<version>.md` must exist first; that is checked before anything
else so a 15-minute build is not spent discovering it is missing. Installing
into `/Applications` is deliberately not automated; the command is printed at
the end.

### Why the order is what it is

Each of these is in the script because it was hit in production:

- **`xcodebuild build` never strips**, and Packer refuses payloads over 1 GB
  with `Bad result len` — which reads like a compression fault rather than "too
  big to ship". Stripping also has to happen *before* signing, so the signature
  covers the final bytes.
- **Signing must precede the DMG and the packages**, because both are built
  from the bundle. Signing afterwards ships an ad-hoc build that Gatekeeper
  refuses the moment it is downloaded and quarantined.
- **The disk image needs its own signature.** A stapled notarization ticket
  alone still assesses as "no usable signature".
- **`get-task-allow` must be removed** before signing; notarization rejects it.
  Entitlements are read back out of the bundle first, because re-signing
  *replaces* them rather than inheriting them — dropping camera, microphone and
  location while the hardened runtime keeps enforcing them.
- **Verify with `spctl`, never `codesign --verify`.** The latter passes on an
  ad-hoc signature and says nothing about Gatekeeper. That is exactly how 7.0.9
  shipped unusable.
- **Both platform keys**, `armac` and `mac`. The binary is universal; Packer's
  `-arch` only picks the output filename.
- **The feed JSON must stay the channel's last message** — `MtpChecker` reads
  history with `limit=1`.

### Doing it by hand

If a step has to be run alone, the individual tools are unchanged:
`build/set_version.py`, `configure.sh` + `xcodebuild`, `strip`,
`~/.claude/skills/macos-codesign/sign.sh`, `create_dmg.sh`,
`~/.claude/skills/macos-codesign/notarize.sh`, `tools/publish_update.py`,
`gh release`. The order above still applies — `release.py` is the encoding of
it, not a replacement for knowing it.

**`create_beautiful_dmg.sh` is not part of this.** It is the fuller variant and
can seed a logged-in session with `INCLUDE_TDATA=1`; a DMG built that way must
never be published.

### Testing an update without spending a version number

The updater's only rule is `availableVersion > AppVersion` **of the running
client**. So lower the tester rather than raising the release: keep one build
pinned at an old version (7.0.0 / `7000000`) and every release you publish
looks newer to it, forever, on the same number.

```bash
rm -rf /tmp/t && mkdir -p /tmp/t/workdir
cp -R /tmp/tlgrm-tester-pristine.app /tmp/t/Tlgrm.app        # keep a pristine copy
cp -R ~/Library/Application\ Support/Tlgrm/tdata /tmp/t/workdir/   # for the MTProto path
/tmp/t/Tlgrm.app/Contents/MacOS/Tlgrm -workdir /tmp/t/workdir
```

The updater replaces the bundle it is **running from**, so always test a copy
and restore it from the pristine one afterwards. Do not kill the client
mid-swap — that leaves an empty bundle.

Two related mechanisms: `publish_update.py --testing` writes the feed's
`testing` key, which only a client that has typed `testupdate` reads (it does
not save a version number, but it keeps a rehearsal off what real installs are
offered); and alpha versions, `7.0.9.1` → `7000009001`, give 999 build numbers
per patch, invisible to stable clients — but the tester must itself be an alpha
build, since a stable client refuses alpha entries outright.

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

### Branches, and one sharp edge

**One GitHub repo (`CelestialTech/tlgrm`) backs both the parent repo and the
`tdesktop` submodule.** They share a branch namespace, so:

- `origin/master` belongs to the **parent**. Pushing the submodule's master
  there would overwrite it. (This has been attempted; git rejected it.)
- No two branches can share a name across the two repos, which is why the
  paired branches below differ.

| Branch | Repo | What |
|---|---|---|
| `master` | parent | Shipping. Pins the submodule commit a release was cut from. |
| `upgrade-v<version>` | submodule | Shipping client work, per upstream base. |
| `telebox` | parent | Separating the local-only surface into its own thing. |
| `tlgrm-desktop` | submodule | The client half of that separation. |

**Release work goes on the shipping branches; everything else does not.** A
build-time check was once fast-forwarded straight onto `upgrade-v7.0.9`, which
coupled unreviewed work to the branch releases are cut from.

**Check which repo you are in before committing.** A `cd` that fails in a
compound command leaves the shell in the previous directory, and a
`git checkout -b` then creates the branch in the wrong repository — silently,
because both are git repos. Prefer `git -C <absolute path>` over relying on the
working directory.

---

## Standing rules

- macOS only. Do not document or "fix" Windows/Linux paths.
- `ENABLE_APP_SANDBOX = NO` on every target.
- No AI attribution in commit messages, ever.
- Use `uv` for Python, never raw `python3`/`pip`.
- MCP: 353 tools, 613 described parameters, four declaration sites must agree — see
  `tdesktop/Telegram/SourceFiles/mcp/mcp_tool_backing.h` for the backing table.
  `tools/check_mcp_tools.py` enforces this at build time.
- **A tool reports what happened; it never records what it wishes had happened.**
  No local table may stand in for a server answer. If no API backs a tool, remove
  it; if one does but is out of reach, fail and say why. The check script cannot
  see this class of bug — it compares declarations, not truth — so it is on
  whoever touches a tool. See `docs/MCP_STAR_GIFTS.md` for the worst cases found
  so far and which ones are still outstanding.
