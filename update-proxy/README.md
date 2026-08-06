# update-proxy

The **Tlgrm auto-update server**. A Pingora/Rust binary (`tlgrm-update-proxy`) that
sits between the desktop client and GitHub Releases.

This is server code. It is not part of the desktop app build, and it is not
browser tooling — see the sibling directories below.

## What it does

| Endpoint | Purpose |
|---|---|
| `/current`, `/current2`, `/current` | Update metadata. Fetches the latest GitHub Release, converts the semver tag to Telegram's version integer (`major*1e6 + minor*1e3 + patch`), and emits Telegram-format JSON keyed by platform (`armac`, `mac`). |
| `/tarmacupd*`, `/tmacupd*` | Stream-proxies the matching release asset from GitHub, stripping headers that would reveal the storage backend. |
| `/webhook/github` | HMAC-verified cache invalidation on release events; purges Cloudflare edge cache on `deleted`. |
| `/health`, `/api/latest` | Status and a friendlier version of the manifest. |
| everything else | Static files from `SITE_ROOT`, when `SITE_ENABLED` (see `../site`). |

## Client contract

The desktop client derives its endpoint from `Platform::AutoUpdateVersion()`, which
is **4** on macOS. So the client requests `{prefix}/current`, where the prefix is
set in `tdesktop/Telegram/SourceFiles/storage/localstorage.cpp`
(`https://updates.71grm.site`). The MTProto fallback path resolves the channel
`updates71grm`.

Packages must be signed with the private key matching `UpdatesPublicKey` in
`tdesktop/Telegram/SourceFiles/config.h`. A package signed with Telegram's key will
fail verification here, and vice versa.

## Status

Source only — **not deployed**. The client half is also inert in current builds:
`DESKTOP_APP_DISABLE_AUTOUPDATE` defaults ON for non-official builds, so
`TDESKTOP_DISABLE_AUTOUPDATE` is defined and `UpdaterDisabled()` returns true.

## Do not resurrect the nginx/Lua variant

An older implementation of the same contract exists on the `aus-wip-6.9.6` branch
at `deploy/update-proxy/` (nginx + Lua). It was deliberately **not** ported to the
7.0.7 base, for two reasons:

1. Its `server_name` is `updates.tlgrm.app` — **a domain we do not own.** That name
   resolves to a third party and redirects to `tlgrm.ru`. Pointing an update
   channel at someone else's domain would let them serve the update manifest.
2. Its binary handler 302-redirects to github.com, which defeats the origin
   concealment this implementation provides.

The domain is settled: **`updates.71grm.site`**, which is registered to us
(Namecheap, Cloudflare DNS). The client agrees — see `readAutoupdatePrefixRaw()` in
`storage/localstorage.cpp`. The `updates.` subdomain still needs an A/CNAME record
creating in Cloudflare; the apex `71grm.site` already resolves.

Note the client persists this prefix in local settings after first run, so changing
it later does not migrate existing installs.

## Configuration

All runtime values come from environment variables — see `.env.example` and
`src/config.rs`. Notable: `GITHUB_REPO`, `PUBLIC_DOMAIN`, `CACHE_TTL_SECS`,
`WEBHOOK_SECRET`, `CLOUDFLARE_ZONE_ID`, `SITE_ROOT`, `SITE_ENABLED`.

## Related

- `../site` — the 71grm.site landing page this binary can serve
- `../tools/site-preview` — browser tooling used to author the site's 3D assets
