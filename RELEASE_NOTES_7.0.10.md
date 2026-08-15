# Tlgrm 7.0.10

Previous release: **7.0.9**.

A fork release on the same Telegram Desktop 7.0.9 base. No upstream code
changed; everything here is Tlgrm's own, and most of it is repair to things
that were broken in ways nothing reported.

---

## Updates reached the wrong server

The HTTP update path had never worked. Telegram's config carries an
`autoupdate_url_prefix` on every load and upstream stores it verbatim, so
`tdata/prefix` on every install held `https://td.telegram.org/` — Telegram
Desktop's own feed, which will never list a Tlgrm version. `updates.71grm.site`
was never contacted.

It failed silently in the worst way: the check succeeded, found nothing, and
logged nothing, while the MTProto path kept delivering updates so the system
looked healthy. Verifying the server end to end — manifest served, package
downloaded byte-identical over the tunnel — said nothing about which URL the
client was actually asking.

The origin is now pinned. A prefix from the server is ignored rather than
stored, and a prefix file left by an earlier build is discarded on read
instead of pinning the mistake permanently.

## The shipped app was missing entitlements

The strip step introduced during 7.0.9 packaging re-signed with
`codesign --force --sign - --options runtime` and no `--entitlements`.
Re-signing *replaces* entitlements rather than inheriting them, so packaged
builds lost camera, microphone and location while the hardened runtime stayed
on and kept enforcing them — meaning voice messages and calls would fail on
the shipped build while working perfectly for whoever built it.

Entitlements are now read out of the bundle before stripping, passed back on
re-sign, and checked afterwards; if they are missing the run aborts rather
than packaging a crippled build.

## The MCP tool contract now describes the tools that exist

The four declaration sites already agreed on *which* tools exist — 355 across
the registry, handler map, backing table and header. What did not agree was
each tool's schema and the arguments its implementation reads.

- **155 arguments across 101 tools were read but never advertised**, so a
  caller trusting `tools/list` could not reach them at all.
- **72 across 41 tools were advertised but never read.** `configure_ad_filter`
  took `hide_sponsored` and `hide_promoted` while the code read `enabled`,
  `keywords` and `exclude_chats`, so following the schema wrote an empty
  config and returned success.
- **Ids are one type now.** `chat_id` was a string in 22 places and an integer
  in 11, `message_id` 7 and 2, so no caller could write one code path. 58
  sites emit integers. Two fields stay strings deliberately, because they are
  not bounded below 2^53: the authorization session hash, a full uint64, and
  `balance_nano`, where money must not depend on float rounding.
- **All 619 parameters carry a description**, derived from the code — defaults
  read out of the call that consumes each argument, accepted values taken from
  the branches the implementation actually tests.

Input compatibility is preserved: arguments were already read through
`toVariant().toLongLong()`, which accepts either form, so callers passing
string ids keep working. Only responses changed shape.

## Publishing

`tools/publish_update.py` gained the gates its absence had cost:

- It refuses to pack a bundle that is not universal. Previously a host-only
  build was signed happily and advertised to both platform keys, handing every
  Mac of the other architecture a package it could not run.
- An unreachable HTTP origin no longer aborts the run — the two paths are
  independent, and one unplugged machine used to mean the MTProto half never
  ran either. It warns, continues, and exits non-zero so half a release cannot
  read as a whole one.
- A queued message is no longer mistaken for a post. `send_document` returns
  on queueing and the placeholder carries a negative local id, which was being
  written into the feed as an unresolvable location.

## Also

- `create_beautiful_dmg.sh` embedded `~/tdata.zip` — a logged-in Telegram
  session — into the installer whenever that file happened to exist, and its
  postinstall copies it into the home directory of whoever installs the DMG.
  It now requires `INCLUDE_TDATA=1`. No published DMG ever carried one; both
  6.9.6 and 7.0.9 were checked.
- Documentation: four documents taught the arm64-only build that ships nothing
  to Intel Macs; the IPC socket was documented as `/tmp/telegram_mcp.sock` in
  seven places when the client binds `/tmp/tlgrm_mcp.sock`; `BUILD_STANDARDS.md`
  pointed at a script that does not exist. `AGENTS.md` and
  `docs/RELEASE_PIPELINE.md` now state the release sequence and its traps.
