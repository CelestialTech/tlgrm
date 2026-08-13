# Tlgrm auto-update system — verified reference

Audited 2026-08-12 against the working tree at `/Users/pasha/xCode/tlgrm`
(client `AppVersion = 7000009`, `AppVersionStr = "7.0.9"`,
`tdesktop/Telegram/SourceFiles/core/version.h:25-27`) and against what is
actually deployed on `ironforge.local` and `updates.71grm.site`.

Everything below was checked, not inferred. Verification commands are given
per link.

---

## 1. The headline

Two delivery paths exist and both are correctly built and published for
7000009. **Only the MTProto one is actually reached.** The HTTP path — the
Rust origin on ironforge, the Cloudflare tunnel, the whole `update-server/`
subsystem — is never contacted by a real client, because Telegram's own
MTProto config overwrites the autoupdate prefix at runtime and the overwrite
is persisted to disk.

Proof on this machine:

```
$ cat "$HOME/Library/Application Support/Tlgrm/tdata/prefix"
https://td.telegram.org/
```

That file was written 2026-02-14 and has never been reset. See §5.

---

## 2. Client: the HTTP path

### Request sequence

1. `Updater::start()` (`core/update_checker.cpp:1476`) fires on a timer
   (`UpdateDelayConstPart` + rand) and starts **both** an `HttpChecker` and an
   `MtpChecker` (`:1513-1521`).
2. `HttpChecker::start()` (`:718`) builds
   `Local::readAutoupdatePrefix() + "/current"` (`:727`).
   Note the **plain `/current`** — this fork dropped upstream's
   `AutoUpdateVersion()` suffix. Upstream would ask `/current4` on macOS
   10.13+ and `/current2` below (`lib_base/base/platform/mac/base_info_mac.mm:210-215`).
3. `gotResponse()` (`:741`) rejects bodies `>= kMaxResponseSize`
   (1 MiB, `:68`), then tries `parseOldResponse` (the legacy
   `123456:url` one-liner, `:791`) and then `parseResponse` (`:809`).
4. `ParseCommonMap` (`:591`) looks up `Platform::AutoUpdateKey()` at the top
   level, then walks channel types. For a stable build the list is exactly
   `{"stable"}` (`:619-626`); `cInstallBetaVersion()` adds `"beta"`,
   `cAlphaVersion()` adds `"alpha"`.
5. Inside the channel object it reads `"released"` (or `"testing"` when
   `UpdateChecker::test()` was used, `:639`). String or number both parse;
   a string may carry a `version:extra` form and only the part before `:` is
   the version (`:645-656`).
6. `validateLatestUrl` (`:843`) drops anything `<= AppVersion`, then
   `parseResponse` returns `readAutoupdatePrefix() + link` (`:840`).
7. `HttpLoader` saves to `UpdatesFolder() + '/' + ExtractFilename(url)`
   (`:870`). `ExtractFilename` (`:317`) takes the last path segment and
   strips every character outside `[a-zA-Z0-9_-]`.
8. `HttpLoaderActor::sendRequest` (`:912`) sends
   `Range: bytes=<alreadySize>-`. Accepted statuses are 200, 206, 416
   (`:958`).

### JSON shape the client requires

```json
{
  "armac": { "stable": { "released": "7000009", "link": "/tarmacupd7000009" } },
  "mac":   { "stable": { "released": "7000009", "link": "/tmacupd7000009"  } }
}
```

`link` is concatenated onto the prefix, so a leading `/` is required.

### Platform key

`lib_base/base/platform/mac/base_info_mac.mm:217`:

```cpp
QString AutoUpdateKey() {
    if (QSysInfo::currentCpuArchitecture().startsWith("arm")
        || RunningThroughRosetta()) {
        return "armac";
    } else {
        return "mac";
    }
}
```

So Apple Silicon **and Intel binaries under Rosetta** both ask for `armac`;
only a native x86_64 process asks for `mac`.

---

## 3. Client: the MTProto path

`MtpChecker::start()` (`:999`):

1. `MTP::ResolveChannel(&_mtp, "updates71grm", ...)` — a literal channel
   username (`:1010`), again with no `AutoUpdateVersion()` suffix (upstream
   uses `tdhbcfeed` → `tdhbcfeed4`).
2. `MTPmessages_GetHistory(..., limit = 1, ...)` (`:1013-1024`) — **only the
   single newest message**.
3. `parseMessage` requires `mtpc_message` (`:1049`), then `parseText`
   (`:1059`) runs the *same* `ParseCommonMap` over the message text.
4. The channel entry is a string `"<version>:<username>#<postId>"`
   (`:1083-1093`). `start = indexOf(':')`, `post = indexOf('#')`.
5. `MTP::StartDedicatedLoader` downloads the document attached to that post,
   in `kChunkSize` = 128 KiB MTProto chunks
   (`mtproto/dedicated_file_loader.h:51`), saving it under the document's own
   filename.

### Verified live feed state

`https://t.me/s/updates71grm` — "Tlgrm Updates", 1 subscriber:

| post | content |
|---|---|
| #1 | `Channel created` |
| #2 | `Tlgrm 7.0.9 — universal (arm64, x86_64)` + document **`tarmacupd7000009`**, 104.6 MB |
| #3 (newest) | `{"armac":{"stable":{"released":"7000009:updates71grm#2"}},"mac":{"stable":{"released":"7000009:updates71grm#2"}}}` |

Both platform keys point at the same post, so an Intel Mac downloads a file
named `tarmacupd7000009`. That is harmless: `FindUpdateFile`'s regexp
(`:299-309`) matches `tarmacupd\d+` regardless of the running architecture,
and the payload is universal (§6).

---

## 4. Server: what is deployed

### Origin

`update-server/src/main.rs`, a static musl binary, 1249728 bytes at
`/usr/local/bin/tlgrm-updates` on ironforge.

| Route | Handler | Line |
|---|---|---|
| `GET /current` | `current` | `main.rs:276` |
| `GET /current4`, `/current2` | `current` | `main.rs:277-278` |
| `GET /health` | `health` | `main.rs:279` |
| `GET /:name` | `package` | `main.rs:280` |

The manifest is **derived from the directory on every request**
(`newest_per_platform`, `main.rs:96`) — highest version wins per platform,
nothing is cached, an empty directory answers **503** rather than `{}`
(`main.rs:157-162`). Package names are parsed strictly as
`<prefix><digits>` (`Package::parse`, `main.rs:73`), which is also the only
path-traversal guard.

### Live state, verified

```
$ curl -s https://updates.71grm.site/current
{"armac":{"stable":{"link":"/tarmacupd7000009","released":"7000009"}},
 "mac":{"stable":{"link":"/tmacupd7000009","released":"7000009"}}}

$ curl -s https://updates.71grm.site/health
{"ok":true,"packages":[{"file":"tarmacupd7000009","platform":"armac","version":7000009},
 {"file":"tmacupd7000009","platform":"mac","version":7000009}],
 "packages_dir":"/srv/tlgrm-updates"}

$ curl -s -o /dev/null -w '%{http_code}' https://updates.71grm.site/current4
200
```

The key order in the reply (`link` before `released`) is `serde_json`'s
`BTreeMap` sorting — a fingerprint confirming the Rust origin is answering,
not the Worker.

```
$ ssh root@ironforge.local 'ls -la /srv/tlgrm-updates/'
drwxr-xr-x  tlgrm tlgrm        logs
-rw-r--r--  tlgrm tlgrm   77886744  Aug  5 23:14  tarmacupd7000007
-rw-r--r--  root  root   109707300  Aug 11 06:56  tarmacupd7000009
-rw-r--r--  root  root   109707300  Aug 11 06:56  tmacupd7000009
```

The two 7000009 files are byte-identical
(`sha256 5ff33ae3fdd36bf1ef83314bb5ee7377b0c87f299aece5ea52ebdf056aa079cb`).
`tarmacupd7000007` is a leftover; it is shadowed by the newer file and does
no harm. There was never a `tmacupd7000007`, so during 7.0.7 the manifest
carried no `mac` key at all and native-Intel clients got
`"platform 'mac' not found in response"`.

### Deployment layout on ironforge

| Piece | Path |
|---|---|
| Binary | `/usr/local/bin/tlgrm-updates` (root:root 0755) |
| Config | `/etc/conf.d/tlgrm-updates` — `TLGRM_LISTEN=127.0.0.1:8083`, `TLGRM_PACKAGES_DIR=/srv/tlgrm-updates`, `RUST_LOG=info` |
| Service | `/etc/init.d/tlgrm-updates` (OpenRC, `command_user=tlgrm:tlgrm`, `retry="SIGTERM/45/SIGKILL/5"`) |
| Data | `/srv/tlgrm-updates` (tlgrm:tlgrm 0755) |
| Logs | `/srv/tlgrm-updates/logs/updates.{out,err}.log` |
| Rotation | `/etc/logrotate.d/tlgrm-updates` — daily, 7, 50M, **copytruncate** (required: start-stop-daemon holds the fd) |
| Tunnel | `/etc/init.d/cloudflared-tlgrm`, user `cftlgrm`, config `/home/cftlgrm/.cloudflared/config.yml`, tunnel `6bb8cb99-2f48-4634-b824-86a9630dca2a`, metrics `127.0.0.1:36502` |

**Reboot survival: clean.** Both `tlgrm-updates` and `cloudflared-tlgrm` are
in the `default` runlevel (`rc-update show default`), both are `started`, the
binary and both credential files live on the root filesystem (`/dev/sda3`,
2.6G/142G used), `conf.d` is on disk, and `start_pre` re-`checkpath`s the data
and log directories. Nothing lives in tmpfs. Host uptime is 35 days; the
service last restarted 2026-08-06.

The `cloudflared-tlgrm.initd` `depend()` orders it `after tlgrm-updates` so
the hostname never advertises a connection-refused window.

### The Cloudflare Worker

`cloudflare-worker/` is **not in the path** — `/health` proves the Rust origin
answers. But `wrangler.toml:11` and `:32` still declare
`{ pattern = "updates.71grm.site/*", zone_name = "71grm.site" }` in both the
default and `production` environments. A Worker route outranks tunnel DNS, so
a `wrangler deploy` run in that directory takes the hostname over silently.

---

## 5. The prefix hijack (root cause of the dead HTTP path)

`storage/localstorage.cpp:543-558`:

```cpp
const QString &readAutoupdatePrefixRaw() {
    const auto &result = AutoupdatePrefix();
    if (!result.isEmpty()) return result;
    QFile f(autoupdatePrefixFile());          // cWorkingDir() + "tdata/prefix"
    if (f.open(QIODevice::ReadOnly)) {
        const auto value = QString::fromUtf8(f.readAll());
        if (!value.isEmpty()) return AutoupdatePrefix(value);
    }
    return AutoupdatePrefix("https://updates.71grm.site");
}
```

`https://updates.71grm.site` is only the **fallback when the file is absent**.
And the file gets written by `mtproto/mtp_instance.cpp:935-937`:

```cpp
if (const auto prefix = data.vautoupdate_url_prefix()) {
    Local::writeAutoupdatePrefix(qs(*prefix));
}
```

Telegram's production config *does* send that field. Result on this machine:
`tdata/prefix` = `https://td.telegram.org/`, so the client asks
`https://td.telegram.org/current`, which answers 200 with Telegram's legacy
manifest:

```json
{"win": {...}, "mac": {"stable": {"released": "1008015", ...}}, "mac32": {...}, "linux32": {...}}
```

Consequences, both silent to the user:

* **Apple Silicon / Rosetta** (`armac`): key absent →
  `"Update Error: MTP platform 'armac' not found in response."` →
  `HttpChecker` fails, forever.
* **Native Intel** (`mac`): `released = 1008015 < 7000009` →
  `validateLatestUrl` returns an empty string → `done(nullptr)` →
  `_isLatest` fires → the app reports **"you are up to date"**.

Updates still reach users only because `Updater::tryLoaders` (`:1604`) falls
through to the MTProto implementation when the HTTP one has no loader
(`:1644-1647`). The Rust origin could be offline for weeks with zero symptom.

Note also that `Local::writeAutoupdatePrefix` (`:560`) rewrites `tdata/prefix`
only when the value differs, so re-pointing it back by hand is undone on the
next config load.

---

## 6. Package format, signing, and the 7000009 payload

### Format (`_other/packer.cpp` writes it, `update_checker.cpp:327` reads it)

Non-Windows header, 152 bytes:

| Offset | Length | Content |
|---|---|---|
| 0 | 128 | RSA-1024 PKCS#1 v1.5 signature over the SHA-1 below (`NID_sha1`) |
| 128 | 20 | SHA-1 of everything from offset 148 onward |
| 148 | 4 | uncompressed size, **native-endian `int32`** |
| 152 | — | xz stream (`lzma_easy_encoder`, preset `9 \| EXTREME`, `LZMA_CHECK_CRC64`) |

Payload is a `QDataStream` at version `Qt_5_1` (big-endian):
`quint32 version` (or `0x7FFFFFFF` + `quint64 alphaVersion`),
`quint32 fileCount`, then per file
`QString relativeName`, `quint32 size`, `QByteArray data`, `bool executable`.

Output name (`packer.cpp:506-510`):
`tarmacupd<version>` when `-arch arm64`, else `tmacupd<version>`.

### Independently verified for `tarmacupd7000009`

* SHA-1 at offset 128 matches the body: **ok**
  (`f8248ff6af42c54df091a7f3592c4210e6519403`).
* RSA verify-recover with `UpdatesPublicKey` yields exactly
  `3021300906052b0e03021a05000414` + that SHA-1 — a valid PKCS#1 DigestInfo:
  **signature valid under the fork's own key**.
* xz decompresses to 517,496,273 bytes, header version field = **7000009**,
  32 files.
* `Tlgrm.app/Contents/MacOS/Tlgrm` (490,545,296 bytes) and
  `Tlgrm.app/Contents/Frameworks/Updater` are both
  **Mach-O universal, x86_64 + arm64**.

So serving the identical file under both names is correct, not a mistake, and
a 7.0.7 client (`AppVersion 7000007 < 7000009`) finds, downloads, verifies and
unpacks it on **both** Apple Silicon and Intel — *provided it reaches the
prefix*.

One packaging defect in the payload: the localization files are stored at a
doubled path, e.g.
`Tlgrm.app/Contents/Resources/de.lproj/de.lproj/Localizable.strings`
(all seven of de, en, es, it, ko, nl, pt-BR). An in-place update writes them
where macOS will not look.

### Keys

`packer.cpp:21-35` (`PublicKey`, `PublicBetaKey`) are byte-identical to
`config.h:45-59` (`UpdatesPublicKey`, `UpdatesPublicBetaKey`), and both are the
public halves of `DesktopPrivate/packer_private.h` — verified with
`openssl rsa -RSAPublicKey_out`. All four agree. They are **custom**, not
upstream's (`git log -p` shows `MIGJAoGBAMA4ViQ…` replaced by
`MIGJAoGBAPscncWX…`), so official Telegram packages would be rejected and vice
versa. `DesktopPrivate/alpha_private.h` is also present, so `-alpha` packing
works.

`UnpackUpdate` tries the beta key as a fallback if the stable key fails
(`update_checker.cpp:386-404`), so a package signed with either fork key
installs.

---

## 7. Every constant that must agree

| Constant | Client | Packer | Server | Agrees? |
|---|---|---|---|---|
| Manifest path | `"/current"` (`update_checker.cpp:727`) | — | `/current`, `/current4`, `/current2` (`main.rs:276-278`) | yes |
| Autoupdate prefix | `"https://updates.71grm.site"` (`localstorage.cpp:557`) | — | `updates.71grm.site` via tunnel | **NO — overridden at runtime, §5** |
| ARM prefix | `tarmacupd` (`update_checker.cpp:305`) | `tarmacupd%1` (`packer.cpp:507`) | `("tarmacupd","armac")` (`main.rs:60`) | yes |
| Intel prefix | `tmacupd` (`update_checker.cpp:304`) | `tmacupd%1` (`packer.cpp:507`) | `("tmacupd","mac")` (`main.rs:60`) | yes |
| Platform keys | `armac` / `mac` (`base_info_mac.mm:217`) | implied by `-arch` | same two strings (`main.rs:60`) | yes |
| Channel name | `"stable"` (`update_checker.cpp:625`) | — | `"stable"` (`main.rs:141`) | yes |
| Version field | `"released"` (`:639`) | — | `"released"` (`main.rs:142`) | yes |
| `released` type | string or number (`:645-656`) | — | string (`main.rs:142`) | yes |
| `link` leading `/` | prefix + link (`:840`) | — | `format!("/{}", …)` (`main.rs:143`) | yes |
| RSA public key | `config.h:45` | `packer.cpp:21` | n/a | yes |
| RSA beta key | `config.h:53` | `packer.cpp:29` | n/a | yes |
| RSA private key | n/a | `DesktopPrivate/packer_private.h` | n/a | yes (derives to the above) |
| Signature length | 128 (`update_checker.cpp:342`) | 128, asserted (`packer.cpp:466`) | n/a | yes |
| SHA length | 20 | 20 | n/a | yes |
| Packages dir | n/a | n/a | `/srv/tlgrm-updates` (`main.rs:264`, `conf.d`) | yes |
| Listen addr | n/a | n/a | `127.0.0.1:8083` (`main.rs:267`, `conf.d`, tunnel ingress) | yes |
| MTProto feed | `"updates71grm"` (`:1010`) | — | channel exists, populated | yes |
| Max manifest | 1 MiB (`:68`) | — | 135 bytes served | yes |
| Max package | 256 MiB (`dedicated_file_loader.h:52`) | 1 GiB uncompressed guard (`packer.cpp:387`) | none | yes today (104.6 MiB) |
| Chunk size | 128 KiB (`dedicated_file_loader.h:51`) | — | 64 KiB stream buffer (`main.rs:224`) | independent, fine |
| **Range support** | required for resume (`:914-917`) | — | **not implemented** (`main.rs:185`) | **NO — §8** |

---

## 8. Silent failure modes

1. **Prefix hijack** (§5). Nothing user-visible; on Intel it actively reports
   "up to date". Recovery only by deleting `tdata/prefix`, and the next MTProto
   config load rewrites it.
2. **No Range support.** `curl -H 'Range: bytes=1000-2000'` returns
   `HTTP/2 200` with `content-length: 109707300` — no `206`, no
   `Accept-Ranges`. The client keeps a partial file across restarts
   (`validateOutput`, `dedicated_file_loader.cpp:222-248`, truncating to a
   128 KiB boundary and setting `_alreadySize`), reopens it with
   `QIODevice::Append` (`:169`), and requests `bytes=<already>-`. The server
   replays from byte 0, the client appends, `writeChunk` fires `ready()` as
   soon as `already >= size` — so a **corrupt file is handed to
   `UnpackUpdate`**. It fails with `"bad SHA1 hash of update file!"`,
   `ClearAll()` wipes the folder, and the next cycle re-downloads all 104 MB.
   Self-healing, but every interrupted download costs a full extra transfer
   and logs an error that reads like a corrupt release.
3. **No access logging.** The router has no `TraceLayer`;
   `updates.out.log` contains three `tlgrm-updates starting` lines and nothing
   else since 2026-08-06. There is no way to tell from the server whether any
   client has ever fetched `/current` — which is exactly how §5 stayed
   invisible.
4. **Per-platform publishing gap.** `newest_per_platform` emits a key only for
   platforms that have a file, and `/current` returns 200 as long as *one*
   platform is present. Copying only `tarmacupd<v>` leaves native-Intel clients
   with `"platform 'mac' not found"` and a 200 response — exactly what happened
   at 7000007.
5. **MTProto feed is last-message-only.** Any post after the manifest (a note,
   a screenshot, the *next* release's package before its JSON) breaks the
   MTProto path until the JSON is reposted. With the HTTP path already dead,
   that would take updates down completely.
6. **Worker route still declared** (`wrangler.toml:11`). A `wrangler deploy`
   silently reassigns the hostname.
7. `handleTimeout` (`:1586`) resets a stuck checker after 10 s
   (`kUpdaterTimeout`, `:67`) and just reschedules. A permanently failing
   check is an infinite quiet retry loop; nothing surfaces unless
   `Logs::DebugEnabled()`.

---

## 9. How to verify each link

```sh
# Manifest and what the server can see
curl -s https://updates.71grm.site/current
curl -s https://updates.71grm.site/health

# Legacy paths still answer
curl -s -o /dev/null -w '%{http_code}\n' https://updates.71grm.site/current4

# Range support (currently returns 200 + full length — that is the bug)
curl -s -D - -o /dev/null -H 'Range: bytes=0-1023' \
  https://updates.71grm.site/tarmacupd7000009 | head -5

# What is on disk, and service/reboot state
ssh root@ironforge.local 'ls -la /srv/tlgrm-updates/; rc-update show default; \
  rc-service tlgrm-updates status; rc-service cloudflared-tlgrm status; \
  cat /etc/conf.d/tlgrm-updates; tail -5 /srv/tlgrm-updates/logs/updates.out.log'

# MTProto feed: the newest message must be the manifest JSON
curl -s https://t.me/s/updates71grm | grep -o 'js-message_text[^>]*>.\{0,300\}' | tail -1

# Which prefix a client will actually use  <-- check this first, always
cat "$HOME/Library/Application Support/Tlgrm/tdata/prefix"

# Package integrity, end to end (sha1 + RSA + xz + version + arch)
curl -sO https://updates.71grm.site/tarmacupd7000009
python3 - <<'PY'
import hashlib, struct, lzma
d = open('tarmacupd7000009','rb').read()
assert hashlib.sha1(d[148:]).digest() == d[128:148], "sha1 mismatch"
raw = lzma.LZMADecompressor(format=lzma.FORMAT_XZ).decompress(d[152:])
print("version", struct.unpack_from('>I', raw, 0)[0],
      "files", struct.unpack_from('>I', raw, 4)[0],
      "uncompressed", len(raw))
PY
# signature: openssl rsa -RSAPublicKey_in -in <pub> -pubout -out spki.pem
#            openssl pkeyutl -verifyrecover -pubin -inkey spki.pem -in <first 128 bytes>
#            -> 3021300906052b0e03021a05000414 || <sha1 from offset 128>
```

## 10. Publishing checklist (what the current process implies)

1. Pack with `-arch arm64` **and** `-arch x86_64`, or pack once and copy the
   universal package to both names — but always produce *both* names.
2. `scp` both into `/srv/tlgrm-updates/` on ironforge. No restart is needed;
   the manifest is derived per request. Note the files land as `root:root 0644`
   which the `tlgrm` service user can still read.
3. Upload the package to the `updates71grm` channel **first**, note its post
   id, then post the manifest JSON as the **last** message.
4. Verify with the commands in §9 — including `tdata/prefix` on a real client,
   which is the only one that tells you whether the HTTP path is live at all.
