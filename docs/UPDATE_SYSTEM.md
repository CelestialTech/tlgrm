# How updates work

Tlgrm carries Telegram Desktop's own updater, repointed at our infrastructure
and our signing keys. Nothing here talks to Telegram's update servers.

## Discovery — two paths, run at once

`Updater::start()` launches both checkers in parallel
(`core/update_checker.cpp`). Either can supply the update; the check only fails
if **both** fail, so one being unavailable is not an outage.

### HTTP

```
GET https://updates.71grm.site/current4
```

The path is `{prefix}/current` + `Platform::AutoUpdateVersion()`, which is `4`
on current macOS. The prefix is stored per-installation and defaults to
`https://updates.71grm.site` (`storage/localstorage.cpp`,
`readAutoupdatePrefixRaw`).

The response is keyed by platform, then by channel:

```json
{
  "armac": { "stable": { "released": "7000007", "link": "/tarmacupd7000007" } },
  "mac":   { "stable": { "released": "7000007", "link": "/tmacupd7000007"  } }
}
```

`Platform::AutoUpdateKey()` picks the key: `armac` on Apple Silicon or under
Rosetta, `mac` on Intel. If `released` exceeds the running `AppVersion`, the
client downloads `{prefix}` + `link`.

Served by **`update-server/`** running on `ironforge.local` — a static musl
binary that generates this response from the packages in `/srv/tlgrm-updates`,
published through its own Cloudflare tunnel.

`cloudflare-worker/` implements the same protocol from GitHub release assets
and remains in the repository as an alternative origin. Only one can own the
hostname; the Worker's custom-domain binding was removed when ironforge took
it over.

### MTProto

The same information, published as a message in a public channel, so the check
works wherever the client can already reach Telegram — the reason upstream ships
this path at all.

The client resolves `@updates71grm` and reads its **latest** message, which must
be JSON of the same shape with one difference: `released` is not a version but a
**location**.

```json
{ "armac": { "stable": { "released": "7000007:updates71grm#42" } } }
```

That is `<version>:<channel>#<postId>`. The client then fetches post `42` in
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
tools/publish_update.py --version 7000007
```

It packs, refuses to continue unless Packer reports `Signature verified!`,
uploads the package to the channel through the running client's MCP bridge
(packages are ~74 MB, past the Bot API's 50 MB ceiling), reads back the post id,
and posts the feed JSON last.

Upload the same package to the GitHub release as well — the Worker serves
`/current4` from release assets.

## Current status

Both delivery paths are provisioned and serving.

| Path | Where | State |
|---|---|---|
| MTProto | `@updates71grm` | channel exists, public, resolving |
| HTTP | `updates.71grm.site` | ironforge via tunnel, returning a manifest |

Verified end to end: `/current4` returns
`{"armac":{"stable":{"link":"/tarmacupd7000007","released":"7000007"}}}`, and
the package downloads over the public tunnel byte-identical to the signed
original (77,886,744 bytes, matching sha256).

What remains is publishing a release: the packages are served from
`/srv/tlgrm-updates` on ironforge, and the channel still needs its first post.

## Things that failed silently here before

Recorded because each was invisible until specifically looked for:

- The feed pointed at `tlgrmfeed4`, a username nobody owns
- Launching with `--mcp` set `gManyInstance`, which skips every update check
- `packer.cpp` verified against Telegram's public keys, not ours
- `Packer` had never been built, so no package had ever existed
- The Worker was bound to `updates.tlgrm.app`, a domain we do not own
- A client-side timeout reported failure for requests Telegram completed anyway
