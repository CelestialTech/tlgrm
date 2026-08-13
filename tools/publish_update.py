#!/usr/bin/env -S uv run --quiet --script
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Publish a signed update package to the Tlgrm update feed.

Both of the client's update paths read the same release, so both are fed here:

  HTTP     GET https://updates.71grm.site/current  -> served by update-server
           on ironforge from the packages it holds on disk.
  MTProto  the latest message in @updates71grm must be the version JSON, and it
           points at a *post* in that channel carrying the package as a
           document.

The MTProto half is the fiddly one. Its "released" field is not a version
string but a location:

    {"armac": {"stable": {"released": "<version>:<channel>#<postId>"}}}

so the package has to be posted first, its post id read back, and only then can
the JSON be posted -- and the JSON must end up as the channel's *latest*
message, because MtpChecker reads history with limit=1. Post anything to the
channel afterwards and update checks stop working until another JSON is posted.

Uploading goes through the running client's MCP bridge rather than a separate
MTProto library: the packages are ~74 MB, past the Bot API's 50 MB ceiling, and
the client is already an authenticated MTProto endpoint.

Usage:
    tools/publish_update.py --version 7000007 [--app PATH] [--channel updates71grm]
    tools/publish_update.py --version 7000007 --pack-only
"""

from __future__ import annotations

import argparse
import json
import os
import socket
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PACKER = REPO / "tdesktop/out/Release/Packer"
APP = REPO / "tdesktop/out/Release/Tlgrm.app"

# Telegram's own platform keys; AutoUpdateKey() picks one at runtime --
# "armac" on Apple Silicon (including under Rosetta), "mac" on Intel. The
# binary is universal, so both keys are published, each from a package built
# by Packer under that architecture. Publishing only "armac" is what left
# Intel Macs told that no update existed while one did.
PLATFORMS = (
    ("armac", "arm64", "tarmacupd"),
    ("mac", "x86_64", "tmacupd"),
)


class BridgeError(RuntimeError):
    pass


class Bridge:
    """Newline-delimited JSON-RPC to the running client's MCP bridge."""

    def __init__(self) -> None:
        self._sock: socket.socket | None = None
        self._buf = b""
        self._id = 0

    @staticmethod
    def _discover() -> tuple[Path, Path]:
        """Return (socket_path, token_path).

        The bridge writes its socket path to a config file rather than a
        symlink in /tmp, which would let any user on the system reach it. It
        writes there through Qt's ConfigLocation, and on macOS that is
        ~/Library/Preferences -- not ~/.config, which is where this used to
        look and never find anything.
        """
        candidates = [
            Path.home() / "Library/Preferences/tlgrm/mcp_socket_path",
            Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config"))
            / "tlgrm/mcp_socket_path",
        ]
        for cfg in candidates:
            if cfg.is_file():
                sock = Path(cfg.read_text().strip())
                return sock, sock.parent / "auth_token"
        raise BridgeError(
            "No MCP socket path published by the client. Looked in:\n  "
            + "\n  ".join(str(c) for c in candidates)
            + "\nStart Tlgrm and sign in first."
        )

    def connect(self) -> None:
        sock_path, token_path = self._discover()
        if not sock_path.exists():
            raise BridgeError(
                f"MCP socket not found at {sock_path}.\n"
                "Start Tlgrm and sign in first -- publishing needs an "
                "authenticated MTProto session."
            )
        if not token_path.is_file():
            raise BridgeError(f"Auth token not found at {token_path}")
        token = token_path.read_text().strip()

        self._sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self._sock.settimeout(120)
        self._sock.connect(str(sock_path))
        self.call("initialize", {"auth_token": token})

    def call(self, method: str, params: dict | None = None) -> dict:
        assert self._sock is not None
        self._id += 1
        req = {"jsonrpc": "2.0", "id": self._id, "method": method}
        if params is not None:
            req["params"] = params
        self._sock.sendall(json.dumps(req).encode() + b"\n")

        while b"\n" not in self._buf:
            chunk = self._sock.recv(65536)
            if not chunk:
                raise BridgeError("Bridge closed the connection")
            self._buf += chunk
        line, self._buf = self._buf.split(b"\n", 1)
        resp = json.loads(line)
        if "error" in resp:
            raise BridgeError(f"{method}: {resp['error']}")
        return resp.get("result", {})

    def tool(self, name: str, arguments: dict) -> dict:
        result = self.call("tools/call", {"name": name, "arguments": arguments})
        # The bridge wraps tool output in MCP content blocks; unwrap to the
        # tool's own JSON so callers see what the tool actually returned.
        content = result.get("content")
        if isinstance(content, list) and content and "text" in content[0]:
            try:
                return json.loads(content[0]["text"])
            except json.JSONDecodeError:
                return {"raw": content[0]["text"]}
        return result


def strip_and_sign(app: Path) -> None:
    """Strip debug symbols from the bundle's binary and re-sign it.

    `xcodebuild build` leaves DWARF in the executable -- stripping only
    happens under DEPLOYMENT_POSTPROCESSING, which a plain build does not set.
    Unstripped, the universal binary is ~1.5 GB, and Packer refuses any
    payload over its 1 GB ceiling with "Bad result len", which reads as a
    compression fault rather than as "this is too big to ship".

    Stripping invalidates the code signature, so the bundle is re-signed
    ad-hoc -- the same signature it was built with.

    NOTE: this mutates the built bundle in place, and create_dmg.sh copies
    that same bundle. Run this BEFORE building the DMG and the DMG carries the
    stripped 491 MB app; run it after and the DMG carries the 1.56 GB one. The
    order is not enforceable from here, so it is stated in AGENTS.md.
    """
    binary = app / "Contents/MacOS" / app.stem
    if not binary.exists():
        raise SystemExit(f"No executable at {binary}")

    before = binary.stat().st_size
    if before < 600 * 1024 * 1024:
        print(f"Binary is {before / 1e6:.0f} MB, already stripped")
        return

    print(f"Stripping {binary.name} ({before / 1e6:.0f} MB) ...")
    proc = subprocess.run(["strip", "-x", str(binary)],
                          capture_output=True, text=True)
    if proc.returncode != 0:
        raise SystemExit(f"strip failed:\n{proc.stderr.strip()}")

    # Re-signing REPLACES the entitlements; it does not inherit them. Signing
    # without --entitlements silently drops camera, microphone and location,
    # and because --options runtime keeps the hardened runtime on, the OS then
    # denies exactly those things: voice messages and calls fail on the
    # shipped build while the developer's own build works. Read the current
    # entitlements back out of the bundle and sign with them.
    ents = subprocess.run(
        ["codesign", "-d", "--entitlements", ":-", str(app)],
        capture_output=True, text=True,
    )
    entitlements_path = None
    if ents.returncode == 0 and ents.stdout.strip().startswith("<"):
        entitlements_path = app.parent / f"{app.stem}.preserved.entitlements"
        entitlements_path.write_text(ents.stdout)
        print(f"  preserving {ents.stdout.count('<key>')} entitlement(s)")
    else:
        print("  WARNING: could not read entitlements from the bundle; "
              "signing without them")

    sign = ["codesign", "--force", "--sign", "-", "--options", "runtime"]
    if entitlements_path:
        sign += ["--entitlements", str(entitlements_path)]
    sign.append(str(app))
    proc = subprocess.run(sign, capture_output=True, text=True)
    if entitlements_path:
        entitlements_path.unlink(missing_ok=True)
    if proc.returncode != 0:
        raise SystemExit(f"codesign failed:\n{proc.stderr.strip()}")

    # Prove it rather than assume it: a silent drop here is the whole bug.
    after = subprocess.run(
        ["codesign", "-d", "--entitlements", ":-", str(app)],
        capture_output=True, text=True,
    )
    if entitlements_path is not None and "<key>" not in after.stdout:
        raise SystemExit(
            "Re-signing dropped the entitlements. Refusing to continue: the "
            "resulting build would be denied camera, microphone and location.")

    after = binary.stat().st_size
    print(f"  {before / 1e6:.0f} MB -> {after / 1e6:.0f} MB, re-signed ad-hoc")


def require_universal(app: Path) -> None:
    """Refuse to publish a bundle that is not universal.

    `xcodebuild -scheme ... build` without a destination emits a host-only
    binary, and nothing downstream notices: Packer signs it happily, the feed
    advertises both platform keys, and every Mac of the other architecture
    downloads a package it cannot run. The check is one `lipo` call, so there
    is no reason for that failure to be discoverable only by a user.
    """
    binary = app / "Contents/MacOS" / app.stem
    proc = subprocess.run(["lipo", "-info", str(binary)],
                          capture_output=True, text=True)
    if proc.returncode != 0:
        raise SystemExit(f"lipo could not read {binary}:\n{proc.stderr.strip()}")
    found = proc.stdout.strip()
    missing = [a for a in ("x86_64", "arm64") if a not in found]
    if missing:
        raise SystemExit(
            f"{binary.name} is not universal -- missing {', '.join(missing)}.\n"
            f"  lipo says: {found}\n"
            "Rebuild with -destination 'generic/platform=macOS'. Publishing "
            "this would tell every Mac of the missing architecture that an "
            "update exists and then hand it one it cannot run.")
    print(f"Universal check: {found.split(':')[-1].strip()}")


def pack(version: int, app: Path, outdir: Path, arch: str, prefix: str) -> Path:
    """Run Packer for one architecture and return the produced package path."""
    if not PACKER.exists():
        raise SystemExit(
            f"Packer not built at {PACKER}.\n"
            "It is gated behind DESKTOP_APP_SPECIAL_TARGET -- run\n"
            "  echo mac > tdesktop/Telegram/build/target\n"
            "then reconfigure and build the Packer target."
        )
    if not app.exists():
        raise SystemExit(f"App bundle not found at {app}")

    outdir.mkdir(parents=True, exist_ok=True)
    print(f"Packing {app.name} at version {version} for {arch} ...")
    proc = subprocess.run(
        [str(PACKER), "-path", str(app), "-version", str(version),
         "-arch", arch],
        cwd=outdir, capture_output=True, text=True,
    )
    if proc.returncode != 0:
        tail = "\n".join((proc.stdout + proc.stderr).strip().splitlines()[-15:])
        raise SystemExit(f"Packer failed:\n{tail}")
    # Packer verifies its own signature before writing; treat a missing
    # confirmation as failure even on a zero exit.
    if "Signature verified!" not in proc.stdout:
        raise SystemExit(
            "Packer did not report 'Signature verified!'. The public keys in "
            "packer.cpp must match the private keys in "
            "DesktopPrivate/packer_private.h and the keys in config.h."
        )
    package = outdir / f"{prefix}{version}"
    if not package.exists():
        raise SystemExit(f"Packer reported success but {package} is missing")
    print(f"  {package.name}  ({package.stat().st_size / 1e6:.1f} MB)  signature verified")
    return package


def resolve_channel(bridge: Bridge, username: str) -> int:
    chats = bridge.tool("list_chats", {})
    for chat in chats.get("chats", []):
        if (chat.get("username") or "").lower() == username.lower():
            return int(chat["id"])
    raise SystemExit(
        f"Channel @{username} not found in this account's chat list.\n"
        "Create it as a public channel and make sure this account is a member."
    )


def version_string(version: int) -> str:
    """7000009 -> "7.0.9". The integer is what the client compares; this is
    only for the human-readable caption."""
    return f"{version // 1000000}.{version // 1000 % 1000}.{version % 1000}"


def latest_post_id(bridge: Bridge, chat_id: int) -> int | None:
    """Newest post id in the channel, or None if the channel is empty.

    read_messages returns newest-first and names the field "message_id", as a
    string. Reading it as "id" -- which is what this did -- yielded None every
    time, so the wait below could never see the post land and every publish
    timed out after the upload had actually succeeded.
    """
    msgs = bridge.tool("read_messages", {"chat_id": chat_id, "limit": 1})
    items = msgs.get("messages") or []
    if not items or "message_id" not in items[0]:
        return None
    return int(items[0]["message_id"])


def post_package(bridge: Bridge, chat_id: int, package: Path, label: str) -> int:
    """Post one package to the channel and return the post id it landed as."""
    before = latest_post_id(bridge, chat_id)
    bridge.tool("send_document", {
        "chat_id": chat_id,
        "file_path": str(package),
        "caption": label,
    })

    # send_document returns on queueing, not delivery, so wait for a new post
    # id to appear rather than assuming the upload has landed.
    size = package.stat().st_size / 1e6
    print(f"Uploading {package.name} ({size:.1f} MB) ...", flush=True)
    deadline = time.monotonic() + 1800
    while time.monotonic() < deadline:
        time.sleep(5)
        current = latest_post_id(bridge, chat_id)
        if current is not None and current != before:
            print(f"  posted as #{current}")
            return current
    raise SystemExit(
        f"Timed out waiting for {package.name} to appear in the channel. "
        "Check it: if the upload succeeded, the feed JSON still needs posting."
    )


def publish_http(packages: list[Path], host: str, directory: str) -> bool:
    """Copy the packages to the HTTP origin. Returns whether it succeeded.

    The origin derives its manifest from what is on disk, so publishing over
    HTTP is exactly this copy -- there is nothing else to poke afterwards.

    A failure here does NOT abort the run. The two update paths are
    independent: ironforge being unreachable (it is a machine on the local
    network) is no reason to skip the MTProto half, and aborting meant one
    unplugged host left every client with no update at all.
    """
    print(f"\nHTTP origin: copying {len(packages)} package(s) to {host}:{directory}")
    proc = subprocess.run(
        ["scp", "-q", *[str(p) for p in packages], f"{host}:{directory}/"],
        capture_output=True, text=True,
    )
    if proc.returncode != 0:
        print(f"  FAILED: scp to {host}: "
              f"{(proc.stdout + proc.stderr).strip().splitlines()[-1] if (proc.stdout + proc.stderr).strip() else 'no output'}")
        print("  Continuing with the MTProto half; re-run with --skip-http "
              "once the host is reachable to finish the HTTP side.")
        return False
    for package in packages:
        print(f"  {package.name} -> {host}:{directory}/{package.name}")
    return True


def publish(
        version: int,
        packages: dict[str, Path],
        channel: str,
        post_id: int | None = None) -> None:
    """Post the package once, then a feed JSON naming it for every platform.

    Packer's -arch only picks the output filename: the payload is the same
    universal bundle either way, and the two files are byte-identical. So one
    post serves both platform keys, and the client is happy to take it --
    FindUpdateFile() accepts any of the known prefixes, not just the one
    matching its own AutoUpdateKey(). Posting both would upload 110 MB twice
    to say the same thing.

    Order matters: the package must be posted before the JSON can name its
    post id, and the JSON must end up as the channel's *latest* message,
    because MtpChecker reads history with limit=1.
    """
    bridge = Bridge()
    bridge.connect()
    chat_id = resolve_channel(bridge, channel)
    print(f"Channel @{channel} -> chat_id {chat_id}")

    keys = [key for key, _arch, _prefix in PLATFORMS]
    arches = ", ".join(arch for _key, arch, _prefix in PLATFORMS)
    caption = f"Tlgrm {version_string(version)} — universal ({arches})"
    if post_id is None:
        post_id = post_package(bridge, chat_id, packages[keys[0]], caption)
    else:
        # Resuming after the JSON half failed: the package is already up
        # there, and re-posting it would leave two copies of the same 110 MB
        # and an older one the feed does not name.
        print(f"Using the package already posted as #{post_id}")

    feed = {
        key: {"stable": {"released": f"{version}:{channel}#{post_id}"}}
        for key in keys
    }
    bridge.tool("send_message", {
        "chat_id": chat_id,
        "text": json.dumps(feed, separators=(",", ":")),
    })
    print(f"\nFeed JSON posted, pointing {', '.join(keys)} at #{post_id}.")
    print("It must remain the channel's latest message.")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", type=int, required=True,
                    help="AppVersion integer, e.g. 7000007")
    ap.add_argument("--app", type=Path, default=APP)
    ap.add_argument("--channel", default="updates71grm")
    ap.add_argument("--outdir", type=Path, default=REPO / "dmg_build")
    ap.add_argument("--pack-only", action="store_true",
                    help="Produce the packages but do not publish")
    ap.add_argument("--skip-http", action="store_true",
                    help="Skip the HTTP origin, publishing only over MTProto")
    ap.add_argument("--http-host", default="root@ironforge.local",
                    help="ssh destination of the HTTP origin")
    ap.add_argument("--http-dir", default="/srv/tlgrm-updates",
                    help="Package directory the origin serves from")
    ap.add_argument("--no-strip", action="store_true",
                    help="Pack the bundle as built, without stripping it")
    ap.add_argument("--post-id", type=int,
                    help="Package already posted as this id; post only the "
                         "feed JSON naming it")
    args = ap.parse_args()

    # Nothing to build when the package is already posted and the HTTP origin
    # is being skipped: the only thing left is the feed JSON. Packing anyway
    # cost two 110 MB builds to post one short message.
    needs_packages = not (args.post_id is not None and args.skip_http)

    packages: dict[str, Path] = {}
    if needs_packages:
        require_universal(args.app)
        if not args.no_strip:
            strip_and_sign(args.app)
        packages = {
            key: pack(args.version, args.app, args.outdir, arch, prefix)
            for key, arch, prefix in PLATFORMS
        }
    if args.pack_only:
        print("\nPackages ready:")
        for key, package in packages.items():
            print(f"  {key:6} {package}")
        return

    # Both of the client's update paths read the same release, so both are
    # fed. HTTP goes first: it is the recoverable half -- a bad copy can be
    # replaced in place, while a channel post that has been read cannot be
    # unposted.
    http_ok = True
    if not args.skip_http:
        http_ok = publish_http(
            list(packages.values()), args.http_host, args.http_dir)
    publish(args.version, packages, args.channel, args.post_id)

    # Exit non-zero if only half the release landed, so a caller (or a human
    # skimming the tail of the output) cannot read partial success as success.
    if not http_ok:
        raise SystemExit(
            "\nMTProto half published; HTTP half did NOT. The release is only "
            "reachable by clients using the MTProto feed until the packages "
            "are copied to the origin.")


if __name__ == "__main__":
    try:
        main()
    except (BridgeError, KeyboardInterrupt) as exc:
        sys.exit(f"error: {exc}")
