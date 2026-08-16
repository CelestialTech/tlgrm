# How updates work

Tlgrm carries Telegram Desktop's own updater, repointed at our infrastructure
and our signing keys. Nothing here talks to Telegram's update servers.

## Discovery — two paths, run at once

`Updater::start()` launches both checkers in parallel
(`core/update_checker.cpp`). Either can supply the update; the check only fails
if **both** fail, so one being unavailable is not an outage.

### HTTP

```
GET https://updates.71grm.site/current
```

The path is plain `{prefix}/current`. Upstream appends
`Platform::AutoUpdateVersion()` (a generation suffix, `4` on current macOS);
that was removed here, so the origin serves `/current` and nothing else.

The prefix is **pinned** to `https://updates.71grm.site`
(`storage/localstorage.cpp`). It is not a per-installation default: Telegram's
config carries an `autoupdate_url_prefix` on every load and upstream stores it
verbatim, which pointed every install at `td.telegram.org` — a feed that will
never list a Tlgrm version. A foreign prefix is now ignored, and a prefix file
left by an older build is discarded on read.

The response is keyed by platform, then by channel:

```json
{
  "armac": { "stable": { "released": "700000901", "link": "/tarmacupd700000901" } },
  "mac":   { "stable": { "released": "700000901", "link": "/tmacupd700000901"  } }
}
```

`Platform::AutoUpdateKey()` picks the key: `armac` on Apple Silicon or under
Rosetta, `mac` on Intel. If `released` exceeds the running `AppVersion`, the
client downloads `{prefix}` + `link`.

Served by **`update-server/`** running on `ironforge.local` — a static musl
binary that generates this response from the packages in `/srv/tlgrm-updates`,
published through its own Cloudflare tunnel.

`cloudflare-worker/` implements the same protocol from GitHub release assets.
It is **superseded**: its custom-domain binding was removed when ironforge took
the hostname over, and nothing routes through it. It stays in the repository
only as a reference implementation.

### MTProto

The same information, published as a message in a public channel, so the check
works wherever the client can already reach Telegram — the reason upstream ships
this path at all.

The client resolves `@updates71grm` and reads its **latest** message, which must
be JSON of the same shape with one difference: `released` is not a version but a
**location**.

```json
{ "armac": { "stable": { "released": "700000901:updates71grm#9" } } }
```

That is `<version>:<channel>#<postId>`. The client then fetches post `9` in
that channel and downloads the document attached to it.

Two consequences worth knowing:

- **The JSON must stay the channel's latest message.** History is read with
  `limit=1`. Anything posted after it stops update checks until a new JSON goes
  up.
- The package post and the JSON post can live in the same channel, provided the
  JSON is posted last.

## The package

`Packer` (`_other/packer.cpp`) takes the built `.app`, compresses it, signs it,
and writes `tarmacupd<version>` (arm64) or `tmacupd<version>` (Intel).

Signing uses the RSA private key in `DesktopPrivate/packer_private.h`. That file
is gitignored and exists only on the build machine; without it no update can
ever be signed again.

The **same public key must appear in two places**, and a mismatch fails
differently in each:

| Where | Purpose | On mismatch |
|---|---|---|
| `_other/packer.cpp` | Packer verifies its own signature before writing | Packing aborts — loud |
| `SourceFiles/config.h` | The client verifies before installing | Packages build fine and every client rejects them — silent |

Packer is only generated when `Telegram/build/target` contains `mac`. Without
that file the target does not exist and no package can be produced — which is
how releases came to ship a DMG the updater cannot consume.

## Install

The client downloads into `tupdates/temp/`, then `checkReadyUpdate()` looks for:

```
tupdates/temp/Tlgrm.app/Contents/Frameworks/Updater
```

Three things must agree on the bundle name, or an update downloads, verifies,
and silently does nothing:

- `output_name` in `Telegram/CMakeLists.txt`
- `appName` in `_other/updater_osx.m`
- the path above in `core/update_checker.cpp`

All three are `Tlgrm`. The `Updater` binary swaps the bundle on next launch.

## Publishing a release

```bash
tools/publish_update.py --version 700000901 --no-strip
```

It refuses to pack a bundle that is not universal, packs both platform keys,
refuses to continue unless Packer reports `Signature verified!`, copies to the
HTTP origin, uploads the package to the channel through the running client's
MCP bridge (packages are ~110 MB, past the Bot API's 50 MB ceiling), reads back
the post id, and posts the feed JSON last.

**The bundle must be Developer ID signed and notarized before this runs** — see
the release steps in AGENTS.md. Packing an unsigned bundle produces an update
that installs an app Gatekeeper refuses once downloaded. `--no-strip` is
required on an already-stripped, signed bundle: stripping forces a re-sign, and
the re-sign here can only be ad-hoc, so the script refuses rather than silently
downgrading the signature.

The GitHub release carries the **DMG**, for people downloading the app by
hand. It plays no part in updates: `/current` is served by the update-server
from `/srv/tlgrm-updates` on ironforge, not from release assets. The
Cloudflare Worker that once read release assets is superseded and no longer
bound to this hostname.

## Current status

Both delivery paths are provisioned, serving, and carrying a release.

| Path | Where | State |
|---|---|---|
| MTProto | `@updates71grm` | feed JSON is the channel's latest message |
| HTTP | `updates.71grm.site` | ironforge via tunnel, serving both platform keys |

Verified end to end on 16 August 2026 with 7.0.9a. `/current` returns:

```json
{"armac":{"stable":{"link":"/tarmacupd700000901","released":"700000901"}},
 "mac":{"stable":{"link":"/tmacupd700000901","released":"700000901"}}}
```

The version integer is `upstreamBase * 100 + forkIndex` — see the versioning
section of AGENTS.md. `700000901` is 7.0.9a: Telegram Desktop 7.0.9, Tlgrm
release `a`. The channel's latest message is the matching feed JSON, pointing
both platform keys at the post carrying the package.

**Both keys must always be present.** The binary is universal; publishing only
`armac` tells every Intel Mac that no update exists while one does. Packer's
`-arch` selects only the output *filename* — the two packages are
byte-identical — so a single channel post can back both keys, because
`FindUpdateFile()` accepts any known prefix regardless of the running
architecture.

## Things that failed silently here before

Recorded because each was invisible until specifically looked for:

- The feed pointed at `tlgrmfeed4`, a username nobody owns
- Launching with `--mcp` set `gManyInstance`, which skips every update check
- `packer.cpp` verified against Telegram's public keys, not ours
- `Packer` had never been built, so no package had ever existed
- The Worker was bound to `updates.tlgrm.app`, a domain we do not own
- A client-side timeout reported failure for requests Telegram completed anyway
