# tlgrm-updates

The HTTP origin for `updates.71grm.site` — the server Telegram Desktop's
updater talks to when it checks for a new version.

A static musl binary, ~2 MB, serving two things from a directory of packages.
No database, no GitHub, no Telegram: it answers from local files, so it keeps
working on the LAN when nothing else does.

## What the client actually asks for

Telegram Desktop's `HttpChecker` asks one question and answers it from one
document:

```cpp
const auto path = Local::readAutoupdatePrefix() + qstr("/current");
```

So the request is `GET https://updates.71grm.site/current`. Upstream appends
`AutoUpdateVersion()` here — `4` on macOS 10.13+, `2` below it — so that old
systems fetch a different document and are never offered a package built
against a toolchain they cannot run. Tlgrm ships one build, so the suffix was
removed; the suffixed paths are still routed for installs predating that
change.

The reply is keyed by platform, then by channel:

```json
{
  "armac": { "stable": { "released": "7000007", "link": "/tarmacupd7000007" } },
  "mac":   { "stable": { "released": "7000007", "link": "/tmacupd7000007"  } }
}
```

`AutoUpdateKey()` picks the key — `armac` on Apple Silicon or under Rosetta,
`mac` on Intel. If `released` exceeds the running `AppVersion`, the client
fetches `{prefix}` + `link`.

That is the whole protocol. This server implements it and nothing else.

## Endpoints

| Route | Purpose |
|---|---|
| `GET /current` | The manifest, generated from disk |
| `GET /current4`, `/current2` | Same manifest, for installs predating the path change |
| `GET /tarmacupd<version>` | The Apple Silicon package |
| `GET /tmacupd<version>` | The Intel package |
| `GET /health` | Liveness, plus the packages it can currently see |

### Range requests

The updater sends `Range: bytes=<alreadySize>-` on **every** package request,
including the first, and accepts 200, 206 and 416
(`update_checker.cpp`, `HttpLoaderActor`). The server honours it:

| Request | Response |
|---|---|
| no `Range` | `200` + `Accept-Ranges: bytes` |
| `bytes=0-` | `206`, whole file, `Content-Range: bytes 0-<n-1>/<n>` |
| `bytes=<k>-` | `206`, the remainder from `k` |
| `bytes=<len>-` | `416` + `Content-Range: bytes */<len>` — the client already has it all |
| `bytes=a-b` | `206`, exactly that slice |
| multi-range, suffix range, malformed | `200`, whole file (a legal answer to a `Range` a server declines to honour) |

Before this, the offset was ignored and an interrupted 110 MB download
restarted from zero. Verified by splitting a real package across two range
requests and confirming the concatenation is byte-identical to the original.

## The manifest is derived, never written

Publishing an update is dropping a file into the packages directory. The
manifest is generated per request by reading that directory and picking the
highest version per platform.

This is the design decision that matters. A hand-written manifest can disagree
with the packages present, and the way that fails is invisible: the client
downloads, verifies, finds nothing, and silently stays on the old version. A
derived manifest cannot drift from the files, because the files are the only
input.

For the same reason, an empty directory answers **503** rather than an empty
manifest. `{}` parses fine and reads as *"you are up to date"*, which would
hide a failed publish indefinitely.

Only names matching `<prefix><digits>` are served. Nothing else in the
directory is reachable, so a key or `.env` placed there by accident cannot be
fetched, and no path traversal is possible — the name never becomes a path
component until it has parsed as a package.

## Build

Cross-compiled on a workstation; the target host keeps no Rust toolchain and no
build tree.

```sh
brew install zig
cargo install cargo-zigbuild
rustup target add x86_64-unknown-linux-musl

cargo zigbuild --release --target x86_64-unknown-linux-musl
# → target/x86_64-unknown-linux-musl/release/tlgrm-updates
```

The target triple must match the host. `ironforge` is **x86_64**; an
`aarch64` build will not run there.

The release profile is tuned for size rather than build speed —
`opt-level = "z"`, `lto = "fat"`, `codegen-units = 1`, `panic = "abort"`,
`strip = "symbols"` — because the binary lives on a small VM where MB and RSS
matter more than how long a release build takes.

Static linking is not incidental: `ldd` reports *"not a valid dynamic
program"*, so the binary has no musl-version coupling to the host. It survives
Alpine upgrades and needs no runtime packages installed.

## Deploy

```sh
scp target/x86_64-unknown-linux-musl/release/tlgrm-updates \
    root@<host>:/usr/local/bin/tlgrm-updates
ssh root@<host> 'chmod 755 /usr/local/bin/tlgrm-updates && rc-service tlgrm-updates restart'
```

First-time host setup is in [`alpine/`](alpine/README.md).

## Configuration

| Variable | Default | Meaning |
|---|---|---|
| `TLGRM_LISTEN` | `127.0.0.1:8083` | Bind address |
| `TLGRM_PACKAGES_DIR` | `/srv/tlgrm-updates` | Where packages live |
| `RUST_LOG` | `info` | Tracing filter |

Set in `/etc/conf.d/tlgrm-updates` so they survive a service-file update.

## Relationship to the Cloudflare Worker

`cloudflare-worker/` implements the same protocol, serving from GitHub release
assets. The two are alternatives, not layers: whichever `updates.71grm.site`
points at is the origin.

This one is chosen when the packages should live on hardware you control and
the update path should not depend on a third party. The Worker is chosen when
there is no host to run, and it costs nothing to keep as a fallback — but only
one can own the hostname at a time.
