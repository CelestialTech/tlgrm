# Rot audit findings — 11 August 2026

53 findings from a six-worker audit of the repository against its own code.


## CRITICAL

- **ARCHITECTURE.md:105** — MCP IPC socket documented as /tmp/telegram_mcp.sock; the client actually binds /tmp/tlgrm_mcp.sock, so every connect example fails.
  <br>*Evidence:* ARCHITECTURE.md:105,263,424,480 '/tmp/telegram_mcp.sock'. core/application.cpp:492 _mcpBridge->start("/tmp/tlgrm_mcp.sock"); mcp/mcp_bridge.cpp:53 returns "/tmp/tlgrm_mcp.sock".
- **BUILD_STANDARDS.md:60** — Points readers at /Users/pasha/xCode/tlgrm/build_with_extracted_api.sh as the automated universal build script; that file does not exist in the repo.
  <br>*Evidence:* BUILD_STANDARDS.md:60 'Use /Users/pasha/xCode/tlgrm/build_with_extracted_api.sh which: 1. Configures... 2. Builds Universal binary'. `ls` -> 'No such file or directory'.
- **README.md:1461** — README's 'Updating to New Telegram Version' section reproduces both build mistakes the fork documents as silent release-killers.
  <br>*Evidence:* L1461 './configure.sh -D TDESKTOP_API_ID=...' with no 'echo mac > build/target' (=> no Packer target, no update package); L1463 'cmake --build . --config Release' with no -destination generic/platform=macOS (=> single-arch binary). Same file L311-315,L372-376 say both are mandatory.
- **create_beautiful_dmg.sh:53** — create_beautiful_dmg.sh packages the builder's own live Telegram session (~/tdata.zip) into initiate.pkg and ships it inside the DMG; its preinstall wipes the recipient's ~/tdata.
  <br>*Evidence:* L53 TDATA_ZIP=$HOME/tdata.zip; L86-92 pkgbuild --root tdata --identifier com.telegram.tlgrm.session --scripts dmg_build/scripts; L133 copies initiate.pkg into DMG staging. dmg_build/scripts/preinstall: 'rm -rf $USER_HOME/tdata'. dmg_build/initiate.pkg exists in-tree.
- **docs/BUILD_GUIDE.md:117** — Documented xcodebuild command omits -destination 'generic/platform=macOS', which silently yields an arm64-only build; same defect at docs/CLAUDE_NOTES.md:327 and docs/MIGRATION_HISTORY.md:309.
  <br>*Evidence:* BUILD_GUIDE.md:117-120 'xcodebuild -project Telegram.xcodeproj -scheme Telegram -configuration Release build'. BUILD_STANDARDS.md:25 '-destination generic/platform=macOS is not optional... the result is arm64-only'.
- **docs/NEXT_STEPS.md:95** — Build command hardcodes -DCMAKE_OSX_ARCHITECTURES=arm64, producing an arm64-only binary in direct violation of the project's universal-binary standard.
  <br>*Evidence:* NEXT_STEPS.md:95 '-DCMAKE_OSX_ARCHITECTURES=arm64 \'. BUILD_STANDARDS.md:5 'ALL builds MUST produce Universal binaries'; tdesktop/cmake/validate_special_target.cmake:26 sets 'x86_64;arm64'.
- **pythonMCP/README.md:143** — Python MCP config documents IPC_SOCKET_PATH=/tmp/telegram_mcp.sock (also :165, :586); real socket is /tmp/tlgrm_mcp.sock, and pythonMCP/src/ipc_bridge.py still defaults to the older /tmp/tdesktop_mcp.sock -- three different paths, none agreeing.
  <br>*Evidence:* pythonMCP/README.md:143 'IPC_SOCKET_PATH=/tmp/telegram_mcp.sock'; :586 'ls -la /tmp/telegram_mcp.sock'. pythonMCP/src/ipc_bridge.py:36 default '/tmp/tdesktop_mcp.sock'. Truth: application.cpp:492 '/tmp/tlgrm_mcp.sock'.
- **tdesktop/Telegram/SourceFiles/mtproto/mtp_instance.cpp:936** — Telegram's MTProto config overwrites the custom autoupdate prefix, so updates.71grm.site is never contacted by any client that has connected to Telegram.
  <br>*Evidence:* configLoadDone: if (const auto prefix = data.vautoupdate_url_prefix()) Local::writeAutoupdatePrefix(qs(*prefix)); -- live proof: ~/Library/Application Support/Tlgrm/tdata/prefix contains 'https://td.telegram.org/'
- **tdesktop/Telegram/SourceFiles/storage/localstorage.cpp:557** — The 71grm default prefix is only a fallback; the persisted tdata/prefix file (td.telegram.org) wins forever once written, and nothing ever resets it.
  <br>*Evidence:* readAutoupdatePrefixRaw reads tdata/prefix first, returns AutoupdatePrefix('https://updates.71grm.site') only if the file is absent/empty. File present on this machine since Feb 14.
- **tools/publish_update.py:175** — Nothing in the release pipeline does Developer ID signing or notarization; the shipped app is ad-hoc signed and the DMG is never signed at all.
  <br>*Evidence:* publish_update.py:175 codesign --force --sign - (ad-hoc). Built bundle: 'Signature=adhoc, TeamIdentifier=not set'. create_dmg.sh has no codesign/notarytool step; grep -rn 'notarytool|Developer ID' outside tdesktop = 0 hits. README:54 sends users to GitHub Releases.
- **tools/publish_update.py:210** — The only loud gate is Packer's 'Signature verified!'; the two failures that silently ship a broken release (non-universal binary, config.h public-key mismatch) are unchecked before packing.
  <br>*Evidence:* L210 checks 'Signature verified!' in Packer stdout. No lipo/ARCHS check on args.app before pack(), despite BUILD_STANDARDS/README:372-376. docs/UPDATE_SYSTEM.md:84-86 states a config.h key mismatch 'builds fine and every client rejects' - nothing verifies it.

## HIGH

- **README.md:54** — No automation whatsoever for GitHub releases, though the README makes GitHub Releases the user-facing download path.
  <br>*Evidence:* grep -rn 'gh release' over repo = 0 hits; git log -S 'gh release' --all = 0 commits; no .github/ in fork root; tdesktop/.github/workflows has no release-publishing action. README:54 links github.com/CelestialTech/tlgrm/releases/latest. Release creation + DMG upload are 100% manual.
- **README.md:336** — README never mentions create_dmg.sh, create_beautiful_dmg.sh or publish_update.py - the entire packaging and publishing stage is undiscoverable from the main doc.
  <br>*Evidence:* grep -rn 'create_dmg|create_beautiful_dmg|publish_update' over repo returns only the scripts' own usage comments, docs/UPDATE_SYSTEM.md:111 and RELEASE_NOTES_7.0.7.md:150. README goes from Step 6 build straight to 'cp Tlgrm.app /Applications' (L410).
- **README.md:87** — README claims '339 tools' in seven places; the registry actually builds 355 tools, so the headline capability number and the tool-backing breakdown are both wrong.
  <br>*Evidence:* README.md:15,87,113,1060,1062,1596,1743,1753 all say 339. mcp_tool_registry.cpp: 348 'Tool{' literals (348 unique names) + 7 'tool.name =' appends = 355.
- **README.md:1749** — README states Version 7.0.7 and Base 'Telegram Desktop 7.0.7' (badge at :59 too); the tree is at 7.0.9 / AppVersion 7000009.
  <br>*Evidence:* README.md:59 'base-tdesktop%207.0.7'; :1749 '**Version**: 7.0.7'; :1750 '**Base**: Telegram Desktop 7.0.7'. Telegram/build/version 'AppVersion 7000009 / AppVersionStr 7.0.9'; core/version.h:25-26.
- **RELEASE_NOTES_7.0.7.md:238** — Release notes state update delivery is not live because the channel and DNS are unprovisioned; UPDATE_SYSTEM.md says both paths are provisioned and verified end to end.
  <br>*Evidence:* RELEASE_NOTES_7.0.7.md:238-241 'The channel @updates71grm and DNS for updates.71grm.site are not yet provisioned, so update delivery is not live.' vs docs/UPDATE_SYSTEM.md:124-134 'Both delivery paths are provisioned and serving' + README:1742 'Auto-update is live'.
- **docs/BUILD_GUIDE.md:110** — Build recipe runs 'cmake -G Xcode ..' by hand from tdesktop/build, bypassing Telegram/configure.sh -- so TDESKTOP_API_ID/HASH and the fork's cmake plumbing are never supplied.
  <br>*Evidence:* BUILD_GUIDE.md:110-114 'cd tdesktop; mkdir -p build && cd build; cmake -G Xcode .. -DCMAKE_BUILD_TYPE=Release'. README.md:317-319 and CLAUDE_NOTES.md:320 use 'cd Telegram && ./configure.sh -D TDESKTOP_API_ID=...'.
- **docs/UPDATE_SYSTEM.md:120** — UPDATE_SYSTEM.md instructs uploading the package to a GitHub release for the Cloudflare Worker, an origin the same file says was unbound 40 lines earlier.
  <br>*Evidence:* L120: 'Upload the same package to the GitHub release as well - the Worker serves /current from release assets.' L39-41: 'Only one can own the hostname; the Worker's custom-domain binding was removed when ironforge took it over.'
- **tdesktop/AGENTS.md:5** — The agent contract for the submodule is written for 'Codex on Windows + WSL' with Docker/Linux builds and CRLF normalization; this fork is macOS-only and builds via Xcode.
  <br>*Evidence:* AGENTS.md:5 '## Working from Codex on Windows + WSL'; :52 'From WSL, run through the Linux Docker build environment'; :166 'On Windows, keep project text files with CRLF'. README.md:1739 '**macOS only** ... No Windows or Linux support.'
- **tdesktop/Telegram/SourceFiles/core/update_checker.cpp:609** — With the hijacked prefix, Apple Silicon clients loop forever on 'platform armac not found' while Intel clients silently read 1008015 as 'up to date' -- neither surfaces anything to the user.
  <br>*Evidence:* td.telegram.org/current returns only win/mac/mac32/linux32 keys, mac released=1008015 < AppVersion 7000009 -> validateLatestUrl returns empty -> isLatest fired.
- **tools/publish_update.py:147** — publish_update.py mutates the built app bundle in place (strip + ad-hoc re-sign), and create_dmg.sh copies that same bundle - so DMG contents depend on undocumented run order.
  <br>*Evidence:* publish_update.py:159-183 strips tdesktop/out/Release/Tlgrm.app/Contents/MacOS/Tlgrm and re-signs; create_dmg.sh:17,59 BUILD_DIR=tdesktop/out/Release, 'cp -R $APP_BUNDLE $DMG_STAGING'. DMG built first can carry a ~1.5GB unstripped binary. No doc states the order.
- **tools/publish_update.py:392** — An unreachable ironforge host aborts the whole publish, including the MTProto half, and the documented command line does not mention the prerequisite or --skip-http.
  <br>*Evidence:* L358 default --http-host root@ironforge.local (LAN mDNS); L291-295 scp failure raises SystemExit; L392-394 publish_http() runs before publish(). docs/UPDATE_SYSTEM.md:110-118 documents only 'tools/publish_update.py --version 7000007'.
- **update-proxy/README.md:3** — Three separate implementations of the same update protocol coexist; the one that opens by calling itself 'The Tlgrm auto-update server' is the one that is NOT deployed.
  <br>*Evidence:* update-proxy/README.md:3 'The **Tlgrm auto-update server**' then :44 'Status: Source only -- not deployed'. docs/UPDATE_SYSTEM.md and tools/publish_update.py both name update-server/ on ironforge as the real origin of updates.71grm.site; cloudflare-worker/ is the standby. update-server/README never mentions update-proxy at all.
- **update-server/src/main.rs:185** — The package handler ignores Range and always answers 200 with the full body, so any interrupted download resumes into a corrupted file that fails SHA1.
  <br>*Evidence:* curl -H 'Range: bytes=1000-2000' -> HTTP/2 200, content-length: 109707300 (no 206, no accept-ranges). Client sends Range and opens the file with QIODevice::Append (dedicated_file_loader.cpp:169,238-246).
- **update-server/src/main.rs:275** — The server writes no access log, so a client that never reaches it or gets a 404 leaves no trace on the server side at all.
  <br>*Evidence:* No TraceLayer/tower-http in the router; /srv/tlgrm-updates/logs/updates.out.log contains only three 'tlgrm-updates starting' lines since 2026-08-06 despite a published release.
- **tdesktop/Telegram/SourceFiles/mcp/batch_operations.cpp:694** — Batch export emits camelCase keys (messageId/chatId/fromId/fromName) while the rest of the surface uses snake_case (message_id/chat_id/from_id).
  <br>*Evidence:* batch_operations.cpp:694-699 json["messageId"]=QString::number(messageId); json["chatId"]=...; json["fromId"]=...; also analytics.cpp:213,277,360,391,1372,1385 userId/chatId.
- **tdesktop/Telegram/SourceFiles/mcp/mcp_analytics_tools.cpp:21** — chat_id is a STRING in every analytics tool result but an INTEGER everywhere else (125 int sites vs 22 string sites).
  <br>*Evidence:* mcp_analytics_tools.cpp:21,27,90,102,148,210,218,270,325,416,475 result["chat_id"]=QString::number(chatId); vs mcp_archive_tools.cpp:52 result["chat_id"]=chatId; and mcp_community_tools.cpp:21 value["chat_id"]=qint64(...)
- **tdesktop/Telegram/SourceFiles/mcp/mcp_core_tools.cpp:433** — message_id is a STRING in read_messages/search_messages but an INTEGER in newer tools (list_rich_messages, list_topics, batch_*) - no caller can write one code path.
  <br>*Evidence:* mcp_core_tools.cpp:433 msg["message_id"]=QString::number(item->id.bare); vs mcp_rich_message_tools.cpp:71 entry["message_id"]=qint64(item->id.bare); Repo-wide: 7 string-typed vs 47 int-typed message_id assignments.
- **tdesktop/Telegram/SourceFiles/mcp/mcp_premium_tools.cpp:446** — configure_ad_filter advertises hide_promoted/hide_sponsored and reads NEITHER; it reads enabled/keywords/exclude_chats which are undeclared - a schema-following caller silently writes an empty config and gets success:true.
  <br>*Evidence:* registry line 1176 props=[hide_promoted,hide_sponsored]; impl mcp_premium_tools.cpp:448-450 args.value("enabled"), args.value("keywords"), args.value("exclude_chats"); :461 result["success"]=true.
- **tdesktop/Telegram/SourceFiles/mcp/mcp_settings_tools.cpp:1659** — backtest_strategy advertises start_date/end_date but reads days/initial_investment/gift_type - the declared date window is ignored entirely.
  <br>*Evidence:* registry:2340 props=[end_date,start_date,strategy]; impl:1661-1663 args.value("days").toInt(30), args.value("initial_investment"), args.value("gift_type").
- **tdesktop/Telegram/SourceFiles/mcp/mcp_settings_tools.cpp:1097** — get_miniapp_spending advertises app_id but reads miniapp_id - passing the documented argument silently returns all miniapps instead of one.
  <br>*Evidence:* registry:2223 props=[app_id]; impl mcp_settings_tools.cpp:1097 QString miniappId = args.value("miniapp_id").toString(); empty -> unfiltered GROUP BY query.
- **tdesktop/Telegram/SourceFiles/mcp/mcp_tool_registry.cpp:2855** — set_chat_rules advertises a rules argument the impl never reads; impl reads rule_name/rule_type/conditions/actions, none declared.
  <br>*Evidence:* registry:2855 props=[chat_id,rules] required=[chat_id]; impl mcp_premium_tools.cpp:498 reads rule_name,rule_type,conditions,actions.
- **tdesktop/Telegram/SourceFiles/mcp/mcp_tool_registry.cpp:2975** — send_gift advertises gift_type/recipient_id/stars_amount but reads gift_id/message/anonymous - the required advertised args do not drive the call.
  <br>*Evidence:* registry:2975 props=[gift_type,recipient_id,stars_amount] required=[recipient_id,stars_amount]; impl mcp_wallet_tools.cpp:280 reads gift_id, message, anonymous (all undeclared).

## MEDIUM

- **.codegraph:0** — 627 MB untracked .codegraph cache with a live daemon holding a 625 MB SQLite db.
  <br>*Evidence:* codegraph.db 625262592 bytes, codegraph.db-shm 15 MB, daemon.pid + daemon.sock present. Untracked (git ls-files .codegraph -> 0) and self-ignored via .codegraph/.gitignore. Regenerable; delete costs only a reindex.
- **DesktopPrivate/custom_api_id.h:1** — DesktopPrivate/ is gitignored as 'NEVER COMMIT' but custom_api_id.h inside it is already tracked; gitignore does not untrack, so the rule is silently violated for that file.
  <br>*Evidence:* git ls-files DesktopPrivate/ -> DesktopPrivate/custom_api_id.h. .gitignore:199 'DesktopPrivate/'. Content is ApiId=2040 (public tdesktop pair) so low actual risk, but the blanket ignore also blocks adding any future needed file.
- **README.md:1749** — README's status block and Step 7 verification commands are stale: version 7.0.7 vs shipped 7.0.9, and Step 7 uses a bundle name that does not exist.
  <br>*Evidence:* README:1749 'Version: 7.0.7'; build/version AppVersion 7000009, core/version.h AppVersion=7000009, Info.plist 7.0.9. README:394,399,402 use 'Telegram.app/Contents/MacOS/Tlgrm' - built bundle is Tlgrm.app (README:410 gets it right). No RELEASE_NOTES_7.0.9.md; newest DMG is Tlgrm_6.9.6.dmg.
- **cloudflare-worker/wrangler.toml:11** — The superseded Worker still declares routes for updates.71grm.site/*, so a stray 'wrangler deploy' would silently take the hostname away from the ironforge origin.
  <br>*Evidence:* routes = [{ pattern = 'updates.71grm.site/*', zone_name = '71grm.site' }] in both the top level and [env.production]; live /health proves the Rust origin currently answers.
- **dmg_build:0** — dmg_build/ holds 1.1 GB of superseded release artifacts: seven DMGs from 6.3.3 through 7.0.9 plus both 7.0.9 update packages.
  <br>*Evidence:* Tlgrm_6.3.3/6.5.0/6.9.3/6.9.4/6.9.5/6.9.6/7.0.9.dmg + tarmacupd7000009 + tmacupd7000009 (109 MB each). Gitignored (.gitignore 'dmg_build/'), so purely local disk. Only dmg_background.png, README_EN/RU.txt, scripts/ and initiate.pkg are inputs the DMG scripts need.
- **docs/UPDATE_SYSTEM.md:136** — UPDATE_SYSTEM.md contradicts itself about whether a release has been published, and is stale against the packages already on disk.
  <br>*Evidence:* L136-137 'What remains is publishing a release ... the channel still needs its first post' vs L124 'Both delivery paths are provisioned and serving'. dmg_build/ holds tarmacupd7000009 and tmacupd7000009 (109.7MB each, Aug 11).
- **site:0** — site/ is entirely untracked yet update-proxy/README.md documents it as the landing page the binary serves, and its dist/assets tree is empty skeleton directories.
  <br>*Evidence:* git ls-files site -> 0 files. site/dist/assets/{fonts,img,models,textures} all contain zero files. update-proxy/README.md 'Related: ../site -- the 71grm.site landing page this binary can serve'. A fresh clone gets neither the site nor an explanation.
- **sweep_results.json:1** — sweep_results.json at repo root is raw untracked output from tools/mcp_test_suite.py left where it was generated.
  <br>*Evidence:* Fields name/kind/err/secs/required/backing match mcp_test_suite.py's OK_OTHER / REFUSED classification vocabulary. git ls-files sweep_results.json -> empty (untracked). Nothing in the repo reads it.
- **test_mcp.py:22** — Root test_mcp.py is dead: it launches out/Release/Telegram.app/Contents/MacOS/Telegram, a path and binary name that stopped existing when the app was renamed Tlgrm and moved under tdesktop/out.
  <br>*Evidence:* test_mcp.py:22 subprocess.Popen(["out/Release/Telegram.app/Contents/MacOS/Telegram", "--mcp"]). Real path is tdesktop/out/Release/Tlgrm.app/Contents/MacOS/Tlgrm (README.md:187). Superseded by tools/mcp_smoke_test.py. Still tracked by git.
- **tools/mcp_smoke_test.py:14** — All three tools/mcp_*.py test scripts hardcode SOCK=/tmp/tlgrm_mcp.sock and TOKEN=/tmp/auth_token, while publish_update.py discovers both via ~/Library/Preferences/tlgrm/mcp_socket_path -- two incompatible conventions for the same bridge.
  <br>*Evidence:* mcp_smoke_test.py:14, mcp_test_suite.py:22, mcp_fixture_test.py:26 all 'SOCK, TOKEN = "/tmp/tlgrm_mcp.sock", "/tmp/auth_token"'. mcp_bridge.cpp:70 puts auth_token next to the socket, and defaultSocketPath() returns CacheLocation/mcp/bridge.sock -- /tmp is only the mkpath-failure fallback.
- **tools/publish_update.py:78** — publish_update.py's docstring claims the bridge avoids /tmp for security, but application.cpp hardcodes the /tmp path, so the secure defaultSocketPath() is dead code.
  <br>*Evidence:* publish_update.py:78 'writes its socket path to a config file rather than a symlink in /tmp, which would let any user on the system reach it'. tdesktop/Telegram/SourceFiles/core/application.cpp:492 _mcpBridge->start("/tmp/tlgrm_mcp.sock") -- Bridge::defaultSocketPath() is never consulted.
- **tools/site-preview/node_modules:0** — 1690 files of node_modules are committed to git under tools/site-preview/.
  <br>*Evidence:* git ls-files tools/site-preview/node_modules | wc -l -> 1690 (of 1699 tracked files in tools/site-preview). No .gitignore covers it; root .gitignore has no node_modules rule.
- **update-server/README.md:118** — Presents cloudflare-worker/ as a co-equal, currently-viable alternative origin ('The Worker is chosen when there is no host to run'); the Worker was superseded by the Rust update-server on ironforge.local.
  <br>*Evidence:* update-server/README.md:118-127 'The two are alternatives, not layers'. docs/UPDATE_SYSTEM.md:129 'HTTP | updates.71grm.site | ironforge via tunnel'; RELEASE_NOTES_7.0.7.md:147 'The Cloudflare Worker was bound to updates.tlgrm.app -- a domain we do not own'.
- **update-server/src/main.rs:60** — Localizable.strings are packed under a doubled .lproj path, so an in-place update installs them where macOS will not find them.
  <br>*Evidence:* Package listing: Tlgrm.app/Contents/Resources/de.lproj/de.lproj/Localizable.strings (all 7 locales doubled) inside tarmacupd7000009.
- **update-server/src/main.rs:96** — Publishing is two independent file copies with no atomicity, and a per-platform gap is invisible: /srv still holds tarmacupd7000007 with no tmacupd7000007 sibling.
  <br>*Evidence:* ls /srv/tlgrm-updates: tarmacupd7000007 (77886744, no Intel counterpart), tarmacupd7000009, tmacupd7000009. newest_per_platform emits a key only for platforms that have a file.
- **tdesktop/Telegram/SourceFiles/mcp/mcp_core_tools.cpp:300** — get_chat_info returns the same identity under two different keys with the same string type (chat_id and id), doubling the contract.
  <br>*Evidence:* mcp_core_tools.cpp:300 chatInfo["chat_id"]=QString::number(chatId); :305 chatInfo["id"]=QString::number(peer->id.value); list_chats (:62) emits only "id", not "chat_id".
- **tdesktop/Telegram/SourceFiles/mcp/mcp_tool_registry.cpp:3034** — Same concept named differently across neighbouring tools: convert_stars declares direction but reads target; get_market_trends/get_earnings_chart declare period but read days; simulate_rating_change declares action/amount but reads additional_stars/additional_reactions.
  <br>*Evidence:* convert_stars reg:3034 props=[direction,stars_amount] vs mcp_wallet_tools.cpp:1182 args["target"]; get_market_trends reg props=[period] vs mcp_settings_tools.cpp:1627 args.value("days"),args.value("gift_type").
- **tdesktop/Telegram/SourceFiles/mcp/mcp_tool_registry.cpp:0** — 135 of 355 tools (38%) have inputSchema/implementation drift; 88 read at least one argument that is never advertised, so those parameters are unreachable through tools/list.
  <br>*Evidence:* Machine diff of registry Tool{} props vs args[".."]/args.value("..") reads per bound handler: 135 tools drift, 88 with reads-undeclared. Full list at /tmp/swarm/rot-audit/mcp_w3.md.

## LOW

- **pythonMCP:0** — pythonMCP carries two rival example configs and a one-off test-repair script, and has not been touched since 2026-02-20 while the C++ MCP surface moved to 7.0.9.
  <br>*Evidence:* Tracked: pythonMCP/config.example.toml AND pythonMCP/config.toml.example (same purpose, two names), plus pythonMCP/fix_tests.py. git log -1 -- pythonMCP/ -> 2026-02-20. Not a duplicate of the C++ server -- README frames it as complementary (AI/ML + Prometheus over the IPC bridge) -- but it is unmaintained relative to it.
- **tdesktop/Telegram/SourceFiles/mtproto/dedicated_file_loader.h:52** — kMaxFileSize is 256 MB and the package grew 77.9 MB -> 109.7 MB across two patch releases, so the ceiling is roughly two more such jumps away.
  <br>*Evidence:* static constexpr auto kMaxFileSize = 256 * 1024 * 1024; tarmacupd7000007=77886744, tarmacupd7000009=109707300 bytes.
