# Tlgrm release pipeline — reconstructed from code (agent w1)

Repo root: `/Users/pasha/xCode/tlgrm`. Submodule: `tdesktop/`. Current source version: **7.0.9 / AppVersion 7000009**.

Everything below is derived from the scripts themselves. Where a document says otherwise, the document is wrong and the discrepancy is called out.

---

## 0. Executive answer to "is there a release script?"

There is no *single* release script. There are **three** artifacts-producing entry points plus a set of manual steps:

| Stage | Mechanism | Automated? |
|---|---|---|
| Configure + build | `tdesktop/Telegram/configure.sh` + `xcodebuild` | manual commands (README:306-370) |
| Update package (`tarmacupd*`/`tmacupd*`) | `tools/publish_update.py` | **yes**, scripted |
| Publish to MTProto channel `@updates71grm` | `tools/publish_update.py` | **yes**, scripted |
| Publish to HTTP origin `updates.71grm.site` | `tools/publish_update.py --http-host` (scp) | **yes**, scripted |
| DMG for humans | `create_dmg.sh` (canonical) | **yes**, scripted |
| GitHub Release + asset upload | *nothing* | **no** — 100% manual `gh` |
| Developer ID signing / notarization | *nothing* | **no** — not done at all |

So: a release script exists (`tools/publish_update.py`, added in `b19769708e`, 2026-08-05) and a DMG script exists (`create_dmg.sh`). What does **not** exist is any `gh release` automation.

---

## 1. Complete ordered sequence to cut a release

### 1.1 Source / version

1. **Set the version** in `tdesktop/Telegram/build/version`:
   ```
   AppVersion         7000009
   AppVersionStrMajor 7.0
   AppVersionStrSmall 7.0.9
   AppVersionStr      7.0.9
   BetaChannel        0
   AlphaVersion       0
   AppVersionOriginal 7.0.9
   ```
2. **Propagate it** with `tdesktop/Telegram/build/set_version.sh <version>` → runs `set_version.py`, which rewrites `tdesktop/Telegram/SourceFiles/core/version.h` (`constexpr auto AppVersion = 7000009; constexpr auto AppVersionStr = "7.0.9";`) and the Windows/setup files. `Info.plist`'s `CFBundleShortVersionString` is derived by CMake from the same source and currently reads `7.0.9`.

   **Lockstep set:** `build/version` ↔ `core/version.h` ↔ `Info.plist` ↔ the `--version` integer passed to `publish_update.py` ↔ the package filename (`tarmacupd<version>`) ↔ the `released` field in the feed JSON. All six must agree; only the first two are mechanically linked.

### 1.2 Configure (the step that silently kills releases)

```bash
cd tdesktop/Telegram
echo mac > build/target      # gitignored upstream — must be recreated per checkout
./configure.sh               # reads DesktopPrivate/custom_api_id.h; -D flags unnecessary
```

`build/target` containing `mac` is what generates the **`Packer`** target *and* turns on `-Werror` (README:311-334, docs/UPDATE_SYSTEM.md:87-89). Without it the build succeeds, the DMG builds, and no update package can ever be produced.

Also required on disk (gitignored, build-machine only):
- `DesktopPrivate/packer_private.h` — the RSA **private** key. Present in the working tree (untracked). If lost, no update can ever be signed again.
- `DesktopPrivate/custom_api_id.h` — API credentials.

### 1.3 Build

```bash
cd tdesktop/out
xcodebuild -project Telegram.xcodeproj -scheme Telegram \
  -configuration Release -destination 'generic/platform=macOS' build -jobs 24
```

`-destination 'generic/platform=macOS'` is mandatory; without it `-scheme` resolves "My Mac" and silently builds one architecture (README:372-379, RELEASE_NOTES_7.0.7.md:228-230, 245-249).

Verify: `lipo -info tdesktop/out/Release/Tlgrm.app/Contents/MacOS/Tlgrm` → must be `x86_64 arm64`. Currently is.

Build products land in **`tdesktop/out/Release/`** — `Tlgrm.app`, `Packer`, `Updater`. (Note: repo-root `out/` does **not** exist; instructions saying `cd ../out` from repo root are wrong.)

Bundle-name triad that must agree or updates install nothing (docs/UPDATE_SYSTEM.md:99-106): `output_name` in `Telegram/CMakeLists.txt`, `appName` in `_other/updater_osx.m`, and the path `tupdates/temp/Tlgrm.app/Contents/Frameworks/Updater` in `core/update_checker.cpp`. Confirmed: `Tlgrm.app/Contents/Frameworks/Updater` exists in the current build.

### 1.4 Pack + publish the update

**Do this before building the DMG** (see §3 ordering hazard).

```bash
tools/publish_update.py --version 7000009
```

What it does, in order (`tools/publish_update.py`):

1. `strip_and_sign()` (L147-183) — if the executable is ≥600 MB, `strip -x` it and re-`codesign --force --sign - --options runtime` the bundle **ad-hoc**. Rationale in the docstring: `xcodebuild build` leaves DWARF in, the unstripped universal binary is ~1.5 GB, and Packer refuses payloads >1 GB with the misleading "Bad result len". Skip with `--no-strip`.
2. `pack()` (L186-220) — runs `tdesktop/out/Release/Packer -path <app> -version <n> -arch {arm64,x86_64}` once per platform key, cwd = `--outdir` (default `dmg_build/`). Produces `tarmacupd<version>` and `tmacupd<version>`. **Gate:** aborts unless Packer's stdout contains `Signature verified!` (L210-215). Aborts if the named file is missing despite exit 0 (L217).
3. `publish_http()` (L281-298) — `scp` both packages to `root@ironforge.local:/srv/tlgrm-updates` (defaults, L358-361). The origin (`update-server/`) derives its manifest from what is on disk, so the copy *is* the HTTP publish. Skip with `--skip-http`.
4. `publish()` (L300-343) — connects to the running client's MCP bridge over a unix socket discovered from `~/Library/Preferences/tlgrm/mcp_socket_path` (+ `auth_token` alongside it), resolves `@updates71grm` via `list_chats`, `send_document`s **one** package (the two are byte-identical; `FindUpdateFile()` accepts either prefix), polls `read_messages limit=1` until a new `message_id` appears (30-min deadline), then `send_message`s the feed JSON:
   ```json
   {"armac":{"stable":{"released":"7000009:updates71grm#<postId>"}},
    "mac":  {"stable":{"released":"7000009:updates71grm#<postId>"}}}
   ```
   **The JSON must remain the channel's latest message** — `MtpChecker` reads history with `limit=1`.

   Recovery: `--post-id <n>` re-posts only the JSON for an already-uploaded package; combined with `--skip-http` it skips packing entirely (L372).

Prerequisite that no document states: **Tlgrm must be running and signed in**, because publishing goes through its MCP bridge.

### 1.5 DMG

```bash
./create_dmg.sh
```

Reads `tdesktop/out/Release/Tlgrm.app`, takes the version from `CFBundleShortVersionString` (falling back to today's date), stages app-only, and runs `create-dmg` (Homebrew) with `dmg_build/dmg_background.png`, window 660×400, app icon at 180,180, Applications link at 480,180, `--no-internet-enable`. Output: `dmg_build/Tlgrm_<version>.dmg`.

Requires `brew install create-dmg`.

### 1.6 GitHub release — entirely manual

There is **no** automation. Evidence of having looked:
- `grep -rn "gh release" .` (excluding `.git`, `.codegraph`) → 0 hits
- `git log -S "gh release" --all` → 0 commits
- no `.github/` directory at the fork root
- `tdesktop/.github/workflows/` contains only upstream CI (`mac.yml`, `mac_packaged.yml`, `linux.yml`, `win.yml`, bot workflows); `grep -rn "action-gh-release|create-release|actions/upload-release"` there → 0 hits

So the step is, by hand:
```bash
gh release create v7.0.9 --repo CelestialTech/tlgrm \
  dmg_build/Tlgrm_7.0.9.dmg dmg_build/tarmacupd7000009 dmg_build/tmacupd7000009
```
(`--repo CelestialTech/tlgrm` is required — the upstream remote otherwise confuses `gh`.)

The update packages only *matter* on the release if `cloudflare-worker/` is serving; it is not (see §5).

### 1.7 What reaches a user's Mac

- **New install:** README:54 → GitHub Releases → `Tlgrm_<v>.dmg` → drag to Applications. The app is **ad-hoc signed, not notarized** (`Signature=adhoc`, `TeamIdentifier=not set`), so Gatekeeper will block it on a fresh Mac.
- **Update:** `Updater::start()` runs the HTTP and MTProto checkers in parallel; either may supply it. Client downloads to `tupdates/temp/`, verifies with the public key in `SourceFiles/config.h`, `checkReadyUpdate()` looks for `tupdates/temp/Tlgrm.app/Contents/Frameworks/Updater`, and the `Updater` binary swaps the bundle on next launch.

---

## 2. `create_dmg.sh` vs `create_beautiful_dmg.sh`

| | `create_dmg.sh` | `create_beautiful_dmg.sh` |
|---|---|---|
| Contents | `Tlgrm.app` + Applications link | app, `README.txt`, `ПРОЧТИ.txt`, **`initiate.pkg`**, Applications link |
| Window | 660×400 | 1024×680 |
| Extra build step | none | builds `initiate.pkg` from `~/tdata.zip` via `pkgbuild` |
| Output name | `dmg_build/Tlgrm_<version>.dmg` | **same path** — they overwrite each other |

**`create_dmg.sh` is canonical.** Its own header says "Minimal DMG creation script … Just the app + background + Applications link."

**`create_beautiful_dmg.sh` must never be used for a public release.** Lines 53-92 zip up `$HOME/tdata.zip` — a live, authenticated Telegram session directory — into `initiate.pkg` with `--identifier com.telegram.tlgrm.session`, and line 133 copies it into the DMG. `dmg_build/scripts/preinstall` then `rm -rf`s the *recipient's* `~/tdata`, and `postinstall` moves the builder's session into the recipient's home and `chown`s it to them. That is a session-transplant installer, not a release DMG. It also claims "GPL v3 LICENSE included" (L184) while copying no LICENSE file.

---

## 3. Ordering hazards and gates

### Loud (release fails visibly)
| Gate | Location |
|---|---|
| Packer must print `Signature verified!` | `tools/publish_update.py:210-215` |
| Packer binary must exist (i.e. `build/target` was set) | `publish_update.py:187-194` |
| App bundle must exist | `publish_update.py:195-196`, `create_dmg.sh:31-34`, `create_beautiful_dmg.sh:26-29` |
| Named package must exist after Packer exit 0 | `publish_update.py:217-218` |
| `strip`/`codesign` non-zero exit | `publish_update.py:171-181` |
| MCP socket / auth token missing | `publish_update.py:92-108` |
| Channel not in the account's chat list | `publish_update.py:228-231` |
| Post never appears within 30 min | `publish_update.py:275-278` |
| `scp` to the origin fails | `publish_update.py:291-295` |
| DMG background image missing | `create_dmg.sh:37-40` |
| `-Werror` (from `build/target`) | build time |

### Silent (release ships broken)
1. **`build/target` absent** → no Packer → you ship a DMG the updater can never consume. Only discovered at publish time.
2. **`config.h` public key ≠ `packer_private.h`** → packages build, `Signature verified!` passes (Packer checks against `packer.cpp`'s copy), and **every client rejects the update**. Nothing in the pipeline checks this. (docs/UPDATE_SYSTEM.md:80-86.)
3. **Non-universal build** → half the users get an unrunnable binary. `publish_update.py` never runs `lipo` before packing.
4. **Anything posted to `@updates71grm` after the feed JSON** → all MTProto update checks stop until a new JSON is posted.
5. **Bundle-name drift** across CMakeLists `output_name` / `updater_osx.m` `appName` / `update_checker.cpp` path → update downloads, verifies, installs nothing.
6. **DMG built before `publish_update.py`** → ships a ~1.5 GB unstripped binary; built after → ships the ad-hoc re-signed stripped one. Undocumented.
7. **No notarization at any point** → Gatekeeper blocks the DMG. The pipeline never invokes `notarytool` or a Developer ID identity.
8. **`--version` typo** → package filename, feed JSON and `AppVersion` disagree; the client either never sees the update or loops on it.

---

## 4. What `publish_update.py` covers and does not

**Covers:** stripping + ad-hoc re-signing the bundle; running Packer for both platform keys; the `Signature verified!` gate; scp to the HTTP origin; MTProto upload through the client's MCP bridge; post-id readback; feed JSON in the correct order; `--pack-only`, `--skip-http`, `--no-strip`, `--post-id` recovery paths.

**Does not cover:** setting or validating the version; configuring or building; verifying the binary is universal; verifying the `config.h` key matches; DMG creation; Developer ID signing; notarization/stapling; creating a GitHub release or uploading any asset; writing release notes; tagging git; restarting/validating `update-server` on ironforge; any post-publish verification that `https://updates.71grm.site/current` now returns the new version.

---

## 5. Delivery infrastructure, actual state

- **HTTP** — `update-server/` (static musl binary) on `ironforge.local`, serving `/srv/tlgrm-updates` through its own Cloudflare tunnel at `updates.71grm.site`. Manifest is generated from files on disk, so the scp *is* the publish.
- **MTProto** — `@updates71grm`, latest message must be the feed JSON.
- **`cloudflare-worker/`** — implements the same protocol from GitHub release assets (`src/worker.ts:150` hits `api.github.com/repos/${GITHUB_REPO}/releases/latest`, indexes assets, skips drafts/prereleases). **Its custom-domain binding was removed** when ironforge took the hostname (docs/UPDATE_SYSTEM.md:39-41). It is a dormant alternative origin. `docs/UPDATE_SYSTEM.md:120` nonetheless still instructs uploading packages to a GitHub release "because the Worker serves `/current` from release assets" — that sentence is stale relative to the paragraph 80 lines above it.

---

## 6. Current-state observations (2026-08-11)

- `tdesktop/Telegram/build/version` and `core/version.h` are both **7000009 / 7.0.9**; `Info.plist` reads 7.0.9.
- `dmg_build/tarmacupd7000009` and `dmg_build/tmacupd7000009` exist (109.7 MB each, built Aug 11) — so 7.0.9 packages have been produced.
- **No `Tlgrm_7.0.9.dmg`.** Newest DMG in `dmg_build/` is `Tlgrm_6.9.6.dmg` (Apr 5). The human-facing artifact for the current version was never built.
- **No `RELEASE_NOTES_7.0.9.md`.** Only `RELEASE_NOTES_7.0.7.md`, which itself still says update delivery is not live.
- `README.md:1749` still says "Version: 7.0.7".
- `Tlgrm.app/Contents/MacOS/Tlgrm` is 490 MB, `x86_64 arm64`, `Signature=adhoc`. At 490 MB it is below `strip_and_sign`'s 600 MB threshold, so `publish_update.py` will report "already stripped" and skip.

---

## 7. Minimal correct release checklist (what should be written down)

```bash
# 1. version
$EDITOR tdesktop/Telegram/build/version
tdesktop/Telegram/build/set_version.sh <AppVersionStr>

# 2. configure (per checkout)
cd tdesktop/Telegram && echo mac > build/target && ./configure.sh

# 3. build universal
cd ../out && xcodebuild -project Telegram.xcodeproj -scheme Telegram \
  -configuration Release -destination 'generic/platform=macOS' build -jobs 24
lipo -info Release/Tlgrm.app/Contents/MacOS/Tlgrm     # must be x86_64 arm64

# 4. (missing today) Developer ID sign bottom-up + notarize + staple

# 5. pack & publish the update — Tlgrm must be RUNNING and signed in
tools/publish_update.py --version <AppVersion>

# 6. DMG (after step 5, so it carries the stripped bundle)
./create_dmg.sh                                        # NOT create_beautiful_dmg.sh

# 7. verify delivery
curl -s https://updates.71grm.site/current             # released == <AppVersion>
#   and confirm the feed JSON is the latest message in @updates71grm

# 8. GitHub release — manual, no automation exists
gh release create v<X.Y.Z> --repo CelestialTech/tlgrm \
  dmg_build/Tlgrm_<X.Y.Z>.dmg dmg_build/tarmacupd<AppVersion> dmg_build/tmacupd<AppVersion>
```
