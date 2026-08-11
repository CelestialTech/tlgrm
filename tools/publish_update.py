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

    proc = subprocess.run(
        ["codesign", "--force", "--sign", "-", "--options", "runtime",
         str(app)],
        capture_output=True, text=True,
    )
    if proc.returncode != 0:
        raise SystemExit(f"codesign failed:\n{proc.stderr.strip()}")

    after = binary.stat().st_size
    print(f"  {before / 1e6:.0f} MB -> {after / 1e6:.0f} MB, re-signed ad-hoc")


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


def latest_post_id(bridge: Bridge, chat_id: int) -> int | None:
    msgs = bridge.tool("read_messages", {"chat_id": chat_id, "limit": 1})
    items = msgs.get("messages") or []
    return int(items[0]["id"]) if items and "id" in items[0] else None


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


def publish_http(packages: list[Path], host: str, directory: str) -> None:
    """Copy the packages to the HTTP origin.

    The origin derives its manifest from what is on disk, so publishing over
    HTTP is exactly this copy -- there is nothing else to poke afterwards.
    """
    print(f"\nHTTP origin: copying {len(packages)} package(s) to {host}:{directory}")
    proc = subprocess.run(
        ["scp", "-q", *[str(p) for p in packages], f"{host}:{directory}/"],
        capture_output=True, text=True,
    )
    if proc.returncode != 0:
        raise SystemExit(
            f"scp to {host} failed:\n{(proc.stdout + proc.stderr).strip()}"
        )
    for package in packages:
        print(f"  {package.name} -> {host}:{directory}/{package.name}")


def publish(version: int, packages: dict[str, Path], channel: str) -> None:
    """Post every package, then one feed JSON naming all of them.

    Order matters twice over. Each package must be posted before the JSON can
    name its post id, and the JSON must end up as the channel's *latest*
    message, because MtpChecker reads history with limit=1. So all packages go
    first and the single JSON goes last -- one JSON covering every platform,
    not one per platform, or only the last would ever be read.
    """
    bridge = Bridge()
    bridge.connect()
    chat_id = resolve_channel(bridge, channel)
    print(f"Channel @{channel} -> chat_id {chat_id}")

    feed = {}
    for key, arch, _prefix in PLATFORMS:
        package = packages[key]
        post_id = post_package(
            bridge, chat_id, package, f"Tlgrm {version} ({arch})")
        feed[key] = {"stable": {"released": f"{version}:{channel}#{post_id}"}}

    bridge.tool("send_message", {
        "chat_id": chat_id,
        "text": json.dumps(feed, separators=(",", ":")),
    })
    print("\nFeed JSON posted, covering: " + ", ".join(feed))
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
    args = ap.parse_args()

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
    if not args.skip_http:
        publish_http(list(packages.values()), args.http_host, args.http_dir)
    publish(args.version, packages, args.channel)


if __name__ == "__main__":
    try:
        main()
    except (BridgeError, KeyboardInterrupt) as exc:
        sys.exit(f"error: {exc}")
