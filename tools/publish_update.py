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

# Telegram's own platform keys; AutoUpdateKey() picks one at runtime. Tlgrm
# builds arm64 only, so a release carries "armac" and Intel Macs are told
# nothing is available rather than being handed an incompatible package.
PLATFORM_KEY = "armac"
PACKAGE_PREFIX = "tarmacupd"


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
        symlink in /tmp, so read that first and fall back to the documented
        default only if it is missing.
        """
        cfg = Path(
            os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config")
        ) / "tlgrm/mcp_socket_path"
        if cfg.is_file():
            sock = Path(cfg.read_text().strip())
        else:
            cache = Path(
                os.environ.get("XDG_CACHE_HOME", Path.home() / "Library/Caches")
            )
            sock = cache / "mcp/bridge.sock"
        return sock, sock.parent / "auth_token"

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


def pack(version: int, app: Path, outdir: Path) -> Path:
    """Run Packer and return the produced package path."""
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
    print(f"Packing {app.name} at version {version} ...")
    proc = subprocess.run(
        [str(PACKER), "-path", str(app), "-version", str(version),
         "-arch", "arm64"],
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
    package = outdir / f"{PACKAGE_PREFIX}{version}"
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


def publish(version: int, package: Path, channel: str) -> None:
    bridge = Bridge()
    bridge.connect()
    chat_id = resolve_channel(bridge, channel)
    print(f"Channel @{channel} -> chat_id {chat_id}")

    before = latest_post_id(bridge, chat_id)
    bridge.tool("send_document", {
        "chat_id": chat_id,
        "file_path": str(package),
        "caption": f"Tlgrm {version} (arm64)",
    })

    # send_document returns on queueing, not delivery, so wait for a new post
    # id to appear rather than assuming the upload has landed.
    print("Uploading (this takes a while for a ~74 MB package) ...", flush=True)
    post_id = None
    deadline = time.monotonic() + 1800
    while time.monotonic() < deadline:
        time.sleep(5)
        current = latest_post_id(bridge, chat_id)
        if current is not None and current != before:
            post_id = current
            break
    if post_id is None:
        raise SystemExit(
            "Timed out waiting for the package post to appear. Check the "
            "channel: if the upload succeeded, re-run with --post-id <id>."
        )
    print(f"Package posted as #{post_id}")

    feed = {PLATFORM_KEY: {"stable": {
        "released": f"{version}:{channel}#{post_id}"
    }}}
    bridge.tool("send_message", {
        "chat_id": chat_id,
        "text": json.dumps(feed, separators=(",", ":")),
    })
    print("Feed JSON posted. It must remain the channel's latest message.")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", type=int, required=True,
                    help="AppVersion integer, e.g. 7000007")
    ap.add_argument("--app", type=Path, default=APP)
    ap.add_argument("--channel", default="updates71grm")
    ap.add_argument("--outdir", type=Path, default=REPO / "dmg_build")
    ap.add_argument("--pack-only", action="store_true",
                    help="Produce the package but do not publish")
    args = ap.parse_args()

    package = pack(args.version, args.app, args.outdir)
    if args.pack_only:
        print(f"\nPackage ready: {package}")
        print("Upload it to the GitHub release as well -- the Cloudflare "
              "worker serves /current from release assets.")
        return
    publish(args.version, package, args.channel)


if __name__ == "__main__":
    try:
        main()
    except (BridgeError, KeyboardInterrupt) as exc:
        sys.exit(f"error: {exc}")
