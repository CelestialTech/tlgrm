# Alpine deployment

`provision.sh` brings an Alpine host to the state `tlgrm-updates` needs: the
service user, the data directory, the OpenRC service, log rotation, and
optionally a cloudflared tunnel. It is idempotent — re-running patches what is
missing and leaves what is there.

It deliberately does **not** build or install the binary. The binary is
cross-compiled on a workstation and shipped separately, so the host keeps no
Rust toolchain and no build tree. See [`../README.md`](../README.md).

## Files

| File | Purpose |
|---|---|
| `provision.sh` | Idempotent provisioner, run as root |
| `tlgrm-updates.initd` | OpenRC service for the update server |
| `tlgrm-updates.logrotate` | Installed at `/etc/logrotate.d/tlgrm-updates` |
| `cloudflared-tlgrm.initd` | OpenRC service for the tunnel (optional) |
| `cloudflared.config.example.yml` | Example tunnel config |

## Bootstrap

```sh
ssh root@<host> 'wget -qO- https://raw.githubusercontent.com/CelestialTech/tlgrm/master/update-server/alpine/provision.sh | sh'
```

Running it from a clone works too, and needs no network:

```sh
doas sh update-server/alpine/provision.sh
```

Then follow the next-steps list it prints.

## Where things live

| Path | Contents |
|---|---|
| `/usr/local/bin/tlgrm-updates` | The binary (shipped, not built here) |
| `/srv/tlgrm-updates/` | Packages — `tarmacupd<version>`, `tmacupd<version>` |
| `/srv/tlgrm-updates/logs/` | `updates.out.log`, `updates.err.log` |
| `/etc/conf.d/tlgrm-updates` | Port and package directory overrides |

## Coexisting with what is already on this host

The intended host already runs `sirenpost` (`:8081`), `telegram-bot-api`
(`:8082`), `tg-s3` (`:9000`), and two cloudflared tunnels (metrics on `36500`
and `36501`). This component takes **`:8083`** and, if tunnelled, metrics
**`36502`**.

Every service here binds loopback and is published through a tunnel; nothing
listens on the LAN except SSH. `tlgrm-updates` follows that, so provisioning it
does not widen the host's exposed surface.

**The tunnel has to be its own.** The existing tunnels belong to a different
Cloudflare account than `71grm.site`, and tunnel credentials are per-account —
so this cannot be an extra `ingress` rule on a running tunnel. Hence a third
service, its own user, its own credentials file.

## Operating it

```sh
rc-service tlgrm-updates start
rc-update add tlgrm-updates default

curl -s http://127.0.0.1:8083/health     # liveness + what it can see
curl -s http://127.0.0.1:8083/current4   # the manifest a client would get
```

Publishing an update is dropping a file in `/srv/tlgrm-updates/`. The manifest
is generated per request from what is there, so nothing needs restarting and
the manifest cannot describe a package that is absent.

`/current4` answers **503** while the directory holds no package. That is
deliberate: an empty manifest parses cleanly and reads as *"you are up to
date"*, which would hide a failed publish for as long as nobody looked.

## Log rotation

`copytruncate` is required rather than stylistic. OpenRC's `start-stop-daemon`
holds the output fd open for the process's lifetime, so a plain rename orphans
the fd and every later write lands in the rotated file. The `sirenpost` config
on the same host uses it for the same reason.

Check the config without rotating:

```sh
logrotate -d /etc/logrotate.d/tlgrm-updates
```
