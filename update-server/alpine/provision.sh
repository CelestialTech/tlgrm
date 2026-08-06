#!/bin/sh
# tlgrm-updates Alpine provisioner.
#
# Run as root on the Alpine host that will serve updates.71grm.site.
# Idempotent: re-running patches what is missing and leaves what is there.
#
#   ssh root@<host> 'wget -qO- https://raw.githubusercontent.com/CelestialTech/tlgrm/master/update-server/alpine/provision.sh | sh'
# or, from a clone:
#   doas sh update-server/alpine/provision.sh
#
# It does NOT build or install the binary. The binary is cross-compiled on a
# workstation and shipped separately (see ../README.md) — this host keeps no
# build tree, which is what keeps it small.
#
# Variables (override via env):
#   TLGRM_USER      (default: tlgrm)
#   TLGRM_DATA      (default: /srv/tlgrm-updates)
#   TLGRM_LISTEN    (default: 127.0.0.1:8083)
#   CF_USER         (default: cftlgrm)
#   REPO_RAW        (default: this repo's raw URL on master)

set -eu

TLGRM_USER="${TLGRM_USER:-tlgrm}"
TLGRM_DATA="${TLGRM_DATA:-/srv/tlgrm-updates}"
TLGRM_LISTEN="${TLGRM_LISTEN:-127.0.0.1:8083}"
CF_USER="${CF_USER:-cftlgrm}"
REPO_RAW="${REPO_RAW:-https://raw.githubusercontent.com/CelestialTech/tlgrm/master/update-server/alpine}"

log() { printf '[provision] %s\n' "$*"; }
die() { printf '[provision] ERROR: %s\n' "$*" >&2; exit 1; }

[ "$(id -u)" -eq 0 ] || die "run as root"

# Fetch a file from the repo, or use the local copy when run from a clone.
# Running from a clone must not require network access.
fetch() {
    _name="$1"; _dest="$2"; _mode="$3"
    _here="$(dirname "$0")/$_name"
    if [ -f "$_here" ]; then
        install -m "$_mode" "$_here" "$_dest"
    else
        wget -qO "$_dest.tmp" "$REPO_RAW/$_name" || die "cannot fetch $_name"
        install -m "$_mode" "$_dest.tmp" "$_dest"
        rm -f "$_dest.tmp"
    fi
}

# --- 1. packages -----------------------------------------------------------
# Deliberately minimal. The binary is statically linked against musl, so it
# needs no runtime libraries at all; logrotate and curl are for operating it.
log "installing packages"
apk add --no-cache logrotate curl >/dev/null

# --- 2. service user -------------------------------------------------------
# No login shell and no home: this account exists only to own the data
# directory and run one process.
if ! id "$TLGRM_USER" >/dev/null 2>&1; then
    log "creating user $TLGRM_USER"
    addgroup -S "$TLGRM_USER"
    adduser -S -D -H -G "$TLGRM_USER" -s /sbin/nologin "$TLGRM_USER"
else
    log "user $TLGRM_USER exists"
fi

# --- 3. data directory -----------------------------------------------------
log "creating $TLGRM_DATA"
install -d -o "$TLGRM_USER" -g "$TLGRM_USER" -m 0755 "$TLGRM_DATA"
install -d -o "$TLGRM_USER" -g "$TLGRM_USER" -m 0755 "$TLGRM_DATA/logs"

# --- 4. OpenRC service -----------------------------------------------------
log "installing OpenRC service"
fetch tlgrm-updates.initd /etc/init.d/tlgrm-updates 0755

# Port and data dir live in conf.d so they survive a service-file update.
if [ ! -f /etc/conf.d/tlgrm-updates ]; then
    log "writing /etc/conf.d/tlgrm-updates"
    cat > /etc/conf.d/tlgrm-updates <<EOF
# Overrides for the tlgrm-updates service.
TLGRM_LISTEN="$TLGRM_LISTEN"
TLGRM_PACKAGES_DIR="$TLGRM_DATA"
RUST_LOG="info"
EOF
else
    log "/etc/conf.d/tlgrm-updates exists, leaving it"
fi

# --- 5. log rotation -------------------------------------------------------
log "installing logrotate config"
fetch tlgrm-updates.logrotate /etc/logrotate.d/tlgrm-updates 0644

# --- 6. cloudflared tunnel user + service ----------------------------------
# The tunnel is optional: a LAN-only deployment needs none of this. Installed
# regardless so enabling it later is one rc-update away, but never started
# here — it cannot run before its credentials exist.
if ! id "$CF_USER" >/dev/null 2>&1; then
    log "creating tunnel user $CF_USER"
    addgroup -S "$CF_USER"
    adduser -S -D -H -G "$CF_USER" -s /sbin/nologin "$CF_USER"
fi
install -d -o "$CF_USER" -g "$CF_USER" -m 0700 "/home/$CF_USER/.cloudflared"
# The tunnel logs here rather than /var/log, which an unprivileged user cannot
# write — start-stop-daemon treats that as a failed start, not a warning.
install -d -o "$CF_USER" -g "$CF_USER" -m 0755 "/home/$CF_USER/logs"

if command -v cloudflared >/dev/null 2>&1; then
    log "cloudflared present at $(command -v cloudflared)"
    fetch cloudflared-tlgrm.initd /etc/init.d/cloudflared-tlgrm 0755
else
    log "cloudflared NOT installed — skipping tunnel service"
fi

# --- 7. report -------------------------------------------------------------
cat <<EOF

[provision] done.

Next steps:

  1. Ship the binary (cross-compiled on a workstation, not built here):
       cargo zigbuild --release --target x86_64-unknown-linux-musl
       scp target/x86_64-unknown-linux-musl/release/tlgrm-updates \\
           root@<host>:/usr/local/bin/tlgrm-updates
       ssh root@<host> 'chmod 755 /usr/local/bin/tlgrm-updates'

  2. Put at least one package in $TLGRM_DATA:
       tarmacupd<version>   Apple Silicon
       tmacupd<version>     Intel
     Until one exists, /current4 answers 503 — deliberately, so an empty
     manifest never reads as "you are up to date".

  3. Start it:
       rc-service tlgrm-updates start
       rc-update add tlgrm-updates default
       curl -s http://$TLGRM_LISTEN/health

  4. Only if this host should serve it publicly, register the tunnel as
     $CF_USER, install the config from cloudflared.config.example.yml, then:
       rc-service cloudflared-tlgrm start
       rc-update add cloudflared-tlgrm default

EOF
