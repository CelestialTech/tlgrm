# Tlgrm 7.0.9

Previous release: **6.9.6** (3 April 2026). Tlgrm 7.0.7 was prepared but never
published, so this release carries both steps.

Tlgrm 6.9.6 was built on Telegram Desktop **6.5.1**. This release is built on
Telegram Desktop **7.0.9** — 28 upstream releases in one step — plus Tlgrm's
own subsystems, repaired and extended.

| | Upstream (6.5.1 → 7.0.9) | Tlgrm's own changes |
|---|---|---|
| Releases spanned | 28 | — |
| Fork commits replayed | — | 23 |
| Files changed by the fork | — | 142 |
| Lines added by the fork | — | +44,893 / −203 |

The detailed account of everything between 6.5.1 and 7.0.7 is in
[RELEASE_NOTES_7.0.7.md](RELEASE_NOTES_7.0.7.md). This document covers what
changed after it.

---

## Part 1 — Upstream, 7.0.7 → 7.0.9

**7.0.8** is a no-op for this fork: three commits, thirteen files — a version
bump, a Windows 7 launch fix, and the removal of 28 unused style entries.

**7.0.9** is 97 commits across 174 files, +7,203 / −934. Notably, the MTProto
schema (`api.tl`) is **byte-identical** to 7.0.7 — every change is client-side
behaviour built on API the client already spoke.

### The article editor keeps growing

- **Rich messages save to HTML** — a message's article content can be written
  out as a self-contained folder, media included (`iv_rich_message_html_export`,
  1,821 new lines), and **imported back** from that same HTML.
- **Media regrouping** — mixed media selections can be regrouped, grouped media
  can be ungrouped from the selection context menu, and a group-type switch
  moves between album, grid and slideshow layouts.
- Slideshows may now exceed 10 items; GIFs are allowed in grouped-media grids.
- Pasting rich HTML blocks and spreadsheet tables into the editor; anchors and
  quote captions survive HTML import.
- The AI composer gained a prompt tab with a custom prompt field, single-use
  tone support, and works without Premium in the article editor.

### Communities

Continued refinement rather than new surface: community userpic in the chats
list top bar, a linked-community row in edit-peer-info, remove-from-community,
correct restoration of pinned community dialogs on launch, notify settings now
saved to the server, swipe-to-ungroup animation, and hashtag search populated
on first click.

### Elsewhere

- Downloads manager accepts external task entries.
- Forum topics: unopened topics included in pull navigation, slide to the next
  unread topic, copied messages grouped by topic ranges.
- Auto-download settings gained master toggles per source.
- Sticker creation editor: keep-aspect-ratio toggle.
- Hardening: miniapp crash fix, blocking-popup guards, MicroTeX formula crashes
  on `smallmatrix` and stray units, GIF layout guarded against empty frames.
- Media libraries updated: dav1d, libavif, libde265, libheif, libjxl.

---

## Part 2 — Tlgrm's own changes

### MCP: 355 tools

Sixteen new tools covering subsystems the 7.0.x base introduced and nothing
exposed. None required a new MTProto method.

- **Communities (8)** — `list_communities`, `get_community`,
  `create_community`, `add_chat_to_community`, `remove_chat_from_community`,
  `set_community_collapsed`, `list_community_join_requests`,
  `review_community_join_request`. Linking a chat that needs admin approval
  reports `pending_approval` rather than failure — the request exists and is
  waiting.
- **Downloads and auto-download (5)** — `list_downloads` (scoped to this
  session; the manager tracks every logged-in account),
  `clear_finished_downloads` and `delete_downloaded_files` kept separate
  because only one is recoverable, plus get/set for auto-download limits.
- **Rich messages (2)** — `list_rich_messages` and `save_rich_message_html`.
  The export overload that takes a destination was private, reachable only
  behind a folder dialog; it is public now, since a headless caller has no one
  to ask.
- **Forum topics (1)** — `list_topics`, with an `unread_only` filter and an
  `unread_known` flag so a zero cannot be misread as "nothing unread".

### The release pipeline, repaired

Three things it believed were no longer true:

- It published only the `armac` platform key. The binary has been universal for
  some time, so **Intel Macs were told no update existed while one did**. Both
  keys are published now.
- It packed the app bundle as built. `xcodebuild build` does not strip, so the
  universal binary was 1.56 GB and Packer rejected it with `Bad result len` —
  which reads like a compression fault rather than "too big to ship".
  Stripping brings it to 491 MB and the packages to 110 MB each.
- It looked for the client's MCP socket under `~/.config`. On macOS Qt writes
  that to `~/Library/Preferences`, so the lookup never found anything.

A fourth defect made every publish appear to fail after succeeding: the post-id
check read the wrong field name, so the wait never saw the package land. A
`--post-id` resume path now recovers from exactly that state.

### Export

One definition of the resume record, written atomically via `fsync` +
`rename()`, with 90 tests covering it.

---

## Verification

- Universal binary confirmed with `lipo`: `x86_64 arm64`.
- Full MCP sweep: 355 tools advertised, 0 schema holes, 0 tools reporting
  success without a backing data source, no crashes.
- Update system verified live on both paths: HTTP manifest at
  `updates.71grm.site/current` serving both platform keys, and the MTProto feed
  in `@updates71grm` as the channel's latest message.

---

## Revision — 15 August 2026

7.0.9 was re-cut after publication. The build now carries a **Developer ID
signature and an Apple notarization ticket**; the first 7.0.9 artifacts were
ad-hoc signed, which runs from a build directory and is refused by Gatekeeper
the moment it is downloaded, since the download is quarantined and ad-hoc
carries no usable signature. `spctl` now reports
`accepted / source=Notarized Developer ID` for both the app and the disk image.

The following also landed in this revision.

### Updates reached the wrong server

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

### The shipped app was missing entitlements

The strip step introduced during 7.0.9 packaging re-signed with
`codesign --force --sign - --options runtime` and no `--entitlements`.
Re-signing *replaces* entitlements rather than inheriting them, so packaged
builds lost camera, microphone and location while the hardened runtime stayed
on and kept enforcing them — meaning voice messages and calls would fail on
the shipped build while working perfectly for whoever built it.

Entitlements are now read out of the bundle before stripping, passed back on
re-sign, and checked afterwards; if they are missing the run aborts rather
than packaging a crippled build.

### The MCP tool contract now describes the tools that exist

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

### Publishing

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

### Also

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
