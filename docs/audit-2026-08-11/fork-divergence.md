# Tlgrm fork divergence and build reference

Audited against `/Users/pasha/xCode/tlgrm/tdesktop`, branch `upgrade-v7.0.9`,
rebased onto upstream tag `v7.0.9`. Range audited: `v7.0.9..HEAD` — 23 commits,
147 files changed, +45938 / −207.

---

## 1. Commit inventory (oldest first)

| # | Commit | Subsystem | Why it exists |
|---|---|---|---|
| 1 | `ac3a8e6bb6` build: fix libheif universal link by pinning local dav1d and disabling gdk-pixbuf | third-party prep | 1 file. `Telegram/build/prepare/prepare.py` — pins `DAV1D_INCLUDE_DIR`/`DAV1D_LIBRARY` to `$USED_PREFIX` and sets `WITH_GDK_PIXBUF=OFF` so libheif links against the locally built universal dav1d instead of a Homebrew host-arch one. Without it the universal link fails. |
| 2 | `0e82c3951b` mcp: port the MCP subsystem onto Telegram Desktop 7.0.7 | MCP (new) | 70 files, +40995. The bulk of the fork. Adds `Telegram/SourceFiles/mcp/**` and wires it into `CMakeLists.txt`, `core/application.*`, `core/launcher.cpp`, `core/sandbox.cpp`. |
| 3 | `66b7e196c9` export: port the gradual export subsystem onto 7.0.7 | export | 20 files, +1407/−166. Gradual/resumable export in `export_api_wrap.cpp` and the whole `export/**` surface, incl. `export.style` and the settings UI. |
| 4 | `4006fc62f6` branding: Tlgrm identity on 7.0.7 | branding | 39 files, +5/−5 plus every icon PNG. `version.h` `AppName`, `Telegram.plist`, `CMakeLists.txt` bundle id/output name, `global_menu_mac.mm`, `window_title_mac.mm`. |
| 5 | `b1f7fd3a36` aus: port the custom auto-update system onto 7.0.7, and fix its compile error | auto-update | `config.h` (RSA keys), `core/update_checker.cpp`, `_other/packer.cpp`, `_other/updater_osx.m`. |
| 6 | `b314c96bb4` aus: repair the update system, dead at four independent layers | auto-update | 3 files, +32/−18. |
| 7 | `3fe2a43841` aus: name the update channel literally, add send_document to publish to it | auto-update + MCP | 6 files, +114/−11. |
| 8 | `c9f5a79979` fix: three silent-failure bugs surfaced by enabling -Werror | correctness | 6 files. `-Werror` itself is upstream (`cmake/options_mac.cmake:42-47`) and only active because `build/target` exists — see §4. |
| 9 | `66f772b607` mcp: record what backs each tool, and refuse the ones nothing backs | MCP | Adds `mcp_tool_backing.{h,cpp}` — the backing table AGENTS.md points at. |
| 10 | `ab4f9ffc52` mcp: classify_intent is real, not unimplemented | MCP | 1-line backing-table correction. |
| 11 | `f7462f387b` mcp: implement the 19 tools that reported success without doing anything | MCP | 7 files, +637/−270. |
| 12 | `e64baf5be9` mcp: route JSON-RPC through callTool, and stop advertising 7 tools twice | MCP | dispatch dedup. |
| 13 | `b230b4c590` mcp: fix a crash-on-missing-argument, and enforce the declared schemas | MCP | 9 files. |
| 14 | `0a97731ed3` mcp: add disposable chat fixtures, and make declared schemas match the code | MCP + test tooling | 5 files. |
| 15 | `04f73c31fe` mcp: fix an archiver that never had a session, and rename the IPC socket | MCP | 12 files, +317/−162. Renames the IPC socket (was `/tmp/tdesktop_mcp.sock`). |
| 16 | `35d69a70b5` mcp: stop nested event loops from outliving what they stand on | MCP | lifetime fix. |
| 17 | `abfbd4ecc7` mcp: gift notifications exist only on broadcast channels | MCP | 1 file. |
| 18 | `ee684113f7` export: one definition of the resume record, written atomically, with tests | export (new) | Adds `export_resume_state.{h,cpp}` + `export_resume_state_tests.cpp` and the `tlgrm_export_tests` target in `cmake/td_export.cmake`. |
| 19 | `ce22236c1e` mcp: give the four tools that asserted from nothing something to assert from | MCP | 3 files. |
| 20 | `ebad9798e1` docs: record what live testing found after the static audit | docs | 1 file. |
| 21 | `e69ae6b94b` aus: drop the generation suffix from the update path | auto-update | 1 file. |
| 22 | `46d4c70477` mcp: make tool names and descriptions say what the tools do | MCP | 8 files. |
| 23 | `34b24b8b38` mcp: cover the subsystems the 7.0.x base brought and nothing exposed | MCP | 10 files, +1049. |

Subsystem tally: MCP 13 commits, auto-update 4, export 2, branding 1, build 1,
correctness 1, docs 1.

---

## 2. Conflict surface for the next upgrade

### 2a. Files the fork MODIFIES that also exist upstream — these conflict

**Build / packaging (5)**
```
Telegram/CMakeLists.txt                  <- mcp/ source list, QtSql, bundle id, output name
Telegram/cmake/td_export.cmake           <- export_resume_state + tlgrm_export_tests target
Telegram/build/prepare/prepare.py        <- dav1d pin, gdk-pixbuf off
Telegram/Telegram.plist                  <- CFBundleGetInfoString
```

**Core / app (6)**
```
Telegram/SourceFiles/core/application.cpp / .h    <- MCP server lifetime
Telegram/SourceFiles/core/launcher.cpp            <- --mcp flag
Telegram/SourceFiles/core/sandbox.cpp
Telegram/SourceFiles/core/version.h               <- AppName = "Tlgrm"
Telegram/SourceFiles/config.h                     <- fork RSA update keys
```

**Auto-update (3)**
```
Telegram/SourceFiles/core/update_checker.cpp
Telegram/SourceFiles/_other/packer.cpp
Telegram/SourceFiles/_other/updater_osx.m
```

**Export — the largest conflict cluster (18)**
```
export/data/export_data_types.h
export/export_api_wrap.cpp / .h
export/export_controller.cpp / .h
export/export_manager.cpp / .h
export/export_settings.cpp / .h
export/output/export_output_abstract.cpp / .h
export/output/export_output_html.cpp / .h
export/output/export_output_html_and_json.cpp / .h
export/view/export_view_panel_controller.cpp / .h
export/view/export_view_settings.cpp / .h
export/view/export.style
```

**Platform / UI (4)**
```
Telegram/SourceFiles/iv/iv_instance.h
Telegram/SourceFiles/platform/mac/global_menu_mac.mm
Telegram/SourceFiles/platform/mac/window_title_mac.mm
Telegram/SourceFiles/storage/localstorage.cpp
Telegram/SourceFiles/window/main_window.cpp
```

**Binary assets (40 PNGs)** — `Telegram/Resources/art/icon*.png`,
`Telegram/Telegram/Images.xcassets/Icon.appiconset/*`,
`Telegram/Telegram/Images.xcassets/Icon.iconset/*`. Always take *ours*; upstream
churn here is cosmetic. Note the asset catalog was renamed `AppIcon` → `Icon`
upstream in 7.0.x — see §6.

### 2b. Files the fork ADDS outright — 75, never conflict

- `Telegram/SourceFiles/mcp/**` — 72 files (the whole MCP subsystem + `REFACTORED.md`)
- `Telegram/SourceFiles/export/export_resume_state.cpp` / `.h` / `_tests.cpp`

An upgrade only has to reconcile the ~36 source files in 2a; everything else
carries forward untouched.

---

## 3. Fork-specific CMake changes (verbatim locations)

`Telegram/CMakeLists.txt`
- after `set_target_properties(Telegram PROPERTIES AUTOMOC ON)`:
  `find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Sql REQUIRED)` — MCP stores
  archive/analytics data in SQLite; `desktop-app::external_qt` does not request
  the Sql component. Deliberately kept in the fork rather than patched into the
  vendored `cmake_helpers` submodule.
- `Qt${QT_VERSION_MAJOR}::Sql` added to `target_link_libraries(Telegram PRIVATE ...)`
- ~line 1418: 72-entry `mcp/...` block inserted into the Telegram source list
  (alphabetically between `main/session/...` and `media/audio/...`).
  **Known wart:** `mcp/mcp_extra_tools.cpp` sits after `mcp/mcp_topic_tools.cpp`,
  out of order.
- ~line 2353: `bundle_identifier` `com.tdesktop.Telegram` → `com.tlgrm.app`
  (both Xcode and non-Xcode branches), `output_name` `Telegram` → `Tlgrm`.

`Telegram/cmake/td_export.cmake`
- `export/export_resume_state.cpp` / `.h` added to `td_export`.
- lines 51-72: `tlgrm_export_tests` executable — headless, Qt Core + lib_base
  only, guarded by `if (NOT DESKTOP_APP_DISABLE_TESTS)`, registered with
  `add_test`. Confirmed present in the generated Xcode project.
  Build and run: `cmake --build . --target tlgrm_export_tests && ./tlgrm_export_tests`.
  Nothing in the release checklist runs it.

`Telegram/build/prepare/prepare.py` (libheif recipe, ~line 1013)
- `-D DAV1D_INCLUDE_DIR=$USED_PREFIX/include/`
- `-D DAV1D_LIBRARY=$USED_PREFIX/lib/libdav1d.a`
- `-D WITH_GDK_PIXBUF=OFF`

---

## 4. `Telegram/build/target` — the invisible switch

**The file is gitignored** (`tdesktop/.gitignore:5` → `/Telegram/build/target`).
It currently exists locally and contains the single word `mac`.

Chain of effect:

1. `cmake/run_cmake.py:49-55` — if the file exists, appends
   `-DDESKTOP_APP_SPECIAL_TARGET=<contents>` to the cmake command line.
   Confirmed in `out/CMakeCache.txt:368` → `DESKTOP_APP_SPECIAL_TARGET:STRING=mac`.
2. `cmake/validate_special_target.cmake:43-44` — on macOS only `mac` and
   `macstore` are accepted; anything else is a `FATAL_ERROR`.
3. `Telegram/CMakeLists.txt:2539` — `if (DESKTOP_APP_SPECIAL_TARGET)` gates
   `add_executable(Packer)`. **No file → no Packer → `publish_update.py` has
   nothing to pack.**
4. `cmake/options_mac.cmake:42-47` — the same variable adds `-g -Werror` to
   `common_options`. So the fork's `-Werror` discipline (commit `c9f5a79979`)
   and the 1.56 GB unstripped debug binary both come from this file.
5. `Telegram/configure.py:55-58` — a non-empty official target makes
   `DesktopPrivate/custom_api_id.h` **mandatory** (`error(...)` and exit 1 if
   missing) and appends `-DTDESKTOP_API_ID` / `-DTDESKTOP_API_HASH` parsed from it.

If you clone fresh, recreate it before configuring:

```bash
echo mac > tdesktop/Telegram/build/target
```

---

## 5. API credentials — what is actually used

`DesktopPrivate/custom_api_id.h` (untracked, parent repo) holds:

```c
static const int32 ApiId = 2040;
static const char *ApiHash = "b18441a1ff607e10a989891a5462e627";
```

Because `build/target` exists, `configure.py` injects those values itself. The
`-D TDESKTOP_API_ID=2040 -D TDESKTOP_API_HASH=b18441a1ff607e10a989891a5462e627`
in the documented configure command is therefore **redundant** — same values,
but they are not the source of truth. `out/CMakeCache.txt:527,530` confirms both
land as `2040` / `b18441a1ff607e10a989891a5462e627`.

`DesktopPrivate/` also holds `alpha_private.h` and `packer_private.h` (the RSA
update-signing keys). Never commit, print, or upload.

---

## 6. Verified build sequence

```bash
# 0. prerequisites, once per clone
echo mac > /Users/pasha/xCode/tlgrm/tdesktop/Telegram/build/target
ls /Users/pasha/xCode/tlgrm/DesktopPrivate/custom_api_id.h   # must exist

# 1. configure
cd /Users/pasha/xCode/tlgrm/tdesktop/Telegram
./configure.sh -D TDESKTOP_API_ID=2040 \
               -D TDESKTOP_API_HASH=b18441a1ff607e10a989891a5462e627

# 2. build UNIVERSAL -- -destination is mandatory
cd /Users/pasha/xCode/tlgrm/tdesktop/out
xcodebuild -project Telegram.xcodeproj -scheme Telegram \
  -configuration Release -destination 'generic/platform=macOS' \
  build -jobs 24

# 3. verify, immediately
lipo -info Release/Tlgrm.app/Contents/MacOS/Tlgrm    # must say: x86_64 arm64
ls -l  Release/Packer Release/Updater                # must exist
```

### The universal-binary trap

`-scheme` makes `xcodebuild` resolve a *concrete* destination — "My Mac", i.e.
arm64 on Apple Silicon — and build that architecture alone, silently ignoring
`ARCHS = (x86_64,arm64)` and `ONLY_ACTIVE_ARCH = NO`, both of which CMake
already sets correctly (232 `ONLY_ACTIVE_ARCH = NO` entries in
`out/Telegram.xcodeproj/project.pbxproj`). `-target` does not have this problem;
only `-scheme` does. This is how 7.0.7 shipped single-architecture.

Documented in three places, all consistent:
- `/Users/pasha/xCode/tlgrm/BUILD_STANDARDS.md:21-34`
- `/Users/pasha/xCode/tlgrm/README.md:365-383`
- `/Users/pasha/xCode/tlgrm/AGENTS.md:33-37`

**Current state verified:** `lipo -info` on
`tdesktop/out/Release/Tlgrm.app/Contents/MacOS/Tlgrm` reports `x86_64 arm64`.
The binary is 490 MB — i.e. already stripped (unstripped is ~1.56 GB).

The 56 `ARCHS = arm64` entries in the pbxproj are **not** a defect: they belong
to host-only codegen targets (`ZERO_CHECK`, `ALL_BUILD`, `install`,
`*_autogen`, `td_scheme_scheme`, `td_lang_lang`, `lib_ui_styles`,
`lib_ui_emoji`, `lib_ui_palette`, `bake_models`, `compile_shaders`) via
`init_host_target`. Only shipped binaries use `init_non_host_target`.

### Sandboxing

Policy: `ENABLE_APP_SANDBOX = NO` on every target.

- `ENABLE_APP_SANDBOX` appears **zero** times in `out/Telegram.xcodeproj/project.pbxproj`
  — compliance is by Xcode default, not by explicit setting.
- The mac build uses `Telegram/Telegram/Telegram.entitlements`
  (`CMakeLists.txt:2360`), which declares only `device.audio-input`,
  `device.camera`, `personal-information.location` — **no** `app-sandbox` key.
- `Telegram/Telegram/Breakpad.entitlements` and
  `Telegram/Telegram/Telegram Lite.entitlements` **do** set
  `com.apple.security.app-sandbox = true`. `Telegram Lite.entitlements` is
  selected only on the `build_macstore` path (`CMakeLists.txt:2339`), which this
  fork does not build. Verdict: **the shipped bundle is unsandboxed and
  compliant**, but the macstore path would violate the rule.

### Code signing — current state

`codesign -dv` on the built app:
```
Identifier=com.tlgrm.app
Format=app bundle with Mach-O universal (x86_64 arm64)
flags=0x10002(adhoc,runtime)
Signature=adhoc
TeamIdentifier=not set
```
Ad-hoc, hardened runtime on, **no entitlements embedded** — see §8.

---

## 7. Manual post-build steps

| Step | Automated? | Documented? | State |
|---|---|---|---|
| Icon fix-up (`iconutil`) | No — manual every build | `AGENTS.md:39-41` | **Broken, see below** |
| Strip before DMG | `publish_update.py --pack-only` does it; ordering is not enforced | `AGENTS.md:47-52` | Correct but fragile |
| DMG build | `create_dmg.sh` | `AGENTS.md:53` | OK |
| Publish packages | `tools/publish_update.py --version 7000009` | `AGENTS.md:58` | OK |
| GitHub release | No | `AGENTS.md:61-66` | Fully manual |
| Run `tlgrm_export_tests` | No | Nowhere | Test target exists, nothing runs it |
| Developer ID sign + notarize | No | Nowhere | Missing entirely |

### The icon step no longer does anything

`AGENTS.md:40` says:
```bash
iconutil --convert icns --output out/Release/Tlgrm.app/Contents/Resources/AppIcon.icns \
  Telegram/Telegram/Images.xcassets/Icon.iconset/
```

But `Telegram/Telegram.plist` sets `CFBundleIconFile = Icon.icns` and
`CFBundleIconName = Icon` — the 7.0.x asset catalog renamed `AppIcon` → `Icon`.
The built bundle carries **both**: `Icon.icns` (12 308 B, produced by actool
from `Icon.appiconset`, plus `Assets.car` 75 128 B) and an orphan
`AppIcon.icns` (89 743 B) that nothing references. The manual step writes a file
macOS never loads.

Correct form, if the step is still needed:
```bash
iconutil --convert icns --output out/Release/Tlgrm.app/Contents/Resources/Icon.icns \
  Telegram/Telegram/Images.xcassets/Icon.iconset/
```
— but the asset-catalog path (`Assets.car` + `CFBundleIconName`) is what macOS
actually reads, so the right fix is to correct the `Icon.appiconset` sources and
drop the manual step altogether.

---

## 8. What silently breaks the build/artifact

Ordered by severity.

1. **CRITICAL — release re-signing drops entitlements.**
   `tools/publish_update.py:180` runs
   `codesign --force --sign - --options runtime <app>` with **no
   `--entitlements`**. `--force` replaces the signature; hardened runtime stays
   on; camera / microphone / location entitlements are gone. Verified: `codesign
   -d --entitlements` on the built bundle returns nothing, though
   `Telegram.entitlements` declares all three. Every published DMG and update
   package ships an app that cannot use the camera or microphone.
   Fix: `--entitlements Telegram/Telegram/Telegram.entitlements`.

2. **HIGH — `Telegram/build/target` is gitignored local-only state.** §4. A
   fresh clone silently builds without `Packer` and without `-Werror`.

3. **HIGH — the icon fix-up is a no-op.** §7.

4. **MEDIUM — the real API-credential dependency is undocumented.** §5.
   `configure.sh` exits 1 if `DesktopPrivate/custom_api_id.h` is missing, and
   `DesktopPrivate/` is untracked; nothing in `AGENTS.md` step 1 says so.

5. **MEDIUM — no Developer ID signing / notarization.** Ad-hoc signature, no
   Team ID. Downloaded DMGs are Gatekeeper-quarantined. A `macos-codesign`
   workflow with a real Developer ID exists on this machine and is unused.

6. **MEDIUM — `tlgrm_export_tests` is built but never run.** §3.

7. **LOW — strip/DMG ordering is unenforced.** `publish_update.py` strips and
   re-signs in place; `create_dmg.sh` copies the same bundle. Run them in the
   wrong order and the DMG ships the 1.56 GB unstripped app. Neither script
   checks.

8. **LOW — `AGENTS.md` numbers two consecutive release steps `5`.**

9. **LOW — `mcp/mcp_extra_tools.cpp` is out of alphabetical order** in the
   `CMakeLists.txt` source list, inviting a duplicate entry on the next merge.

10. **LOW — no build-time universal gate.** The trap is documented three times
    but only `publish_update.py` verifies, at pack time. A host-only build
    survives until packaging.
