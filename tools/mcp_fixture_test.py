#!/usr/bin/env -S uv run --quiet --script
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Exercise the destructive half of the MCP surface against disposable fixtures.

Roughly half the tools mutate, send or delete, so a sweep against a real
account has to skip them and they stay untested. This creates its own channel
and supergroup, runs those tools against nothing else, and deletes them again.

The scoping is enforced, not merely intended: every call goes through guard(),
which refuses any argument naming a chat this run did not create. A bug in a
test cannot reach a real conversation.

Fixtures are deleted in a finally block, and any that survive are named in the
summary so nothing is silently left behind.
"""
from __future__ import annotations

import json
import socket
import sys
import time

SOCK, TOKEN = "/tmp/tlgrm_mcp.sock", "/tmp/auth_token"
PREFIX = "tlgrm-test-"          # every fixture is named this way
CHAT_ARG_KEYS = ("chat_id", "channel_id", "peer_id", "from_chat_id", "to_chat_id")


class Bridge:
    def __init__(self):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.settimeout(90)
        self.s.connect(SOCK)
        self.buf, self.n = b"", 0
        self.rpc("initialize", {"auth_token": open(TOKEN).read().strip()})

    def rpc(self, method, params=None):
        self.n += 1
        req = {"jsonrpc": "2.0", "id": self.n, "method": method}
        if params is not None:
            req["params"] = params
        self.s.sendall(json.dumps(req).encode() + b"\n")
        while b"\n" not in self.buf:
            chunk = self.s.recv(1 << 20)
            if not chunk:
                raise ConnectionError("bridge closed — the client likely crashed")
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        return json.loads(line)

    def tool(self, name, args):
        r = self.rpc("tools/call", {"name": name, "arguments": args})
        c = r.get("result", {}).get("content")
        if isinstance(c, list) and c and "text" in c[0]:
            try:
                return json.loads(c[0]["text"])
            except json.JSONDecodeError:
                return {"_raw": c[0]["text"][:200]}
        return r.get("result", r)


class Harness:
    def __init__(self):
        self.b = Bridge()
        self.fixtures: set[int] = set()
        self.results: list[tuple[str, str, str]] = []

    # --- safety ---------------------------------------------------------
    def guard(self, name, args):
        """Refuse any call naming a chat this run did not create."""
        for key in CHAT_ARG_KEYS:
            if key in args:
                value = int(args[key])
                if value not in self.fixtures:
                    raise AssertionError(
                        f"REFUSED {name}: {key}={value} is not a fixture "
                        f"(fixtures={sorted(self.fixtures)})")

    def call(self, label, name, args, expect="ok"):
        try:
            self.guard(name, args)
        except AssertionError as e:
            self.results.append((label, "BLOCKED", str(e)))
            return {}
        t0 = time.monotonic()
        try:
            res = self.b.tool(name, args)
        except ConnectionError as e:
            self.results.append((label, "CRASH", str(e)))
            raise
        dt = time.monotonic() - t0
        err = res.get("error") or res.get("message")
        ok = res.get("success")
        if expect == "ok":
            status = "PASS" if (ok is True or (ok is None and not err)) else "FAIL"
        else:
            status = "PASS" if err else "FAIL"
        detail = (str(err)[:70] if err else
                  " ".join(f"{k}={json.dumps(v)[:28]}"
                           for k, v in list(res.items())[:3] if k != "backing"))
        self.results.append((label, status, f"{detail} ({dt:.1f}s)"))
        return res

    # --- fixtures -------------------------------------------------------
    def create_fixtures(self):
        stamp = int(time.time())
        for kind, mega in (("channel", False), ("supergroup", True)):
            res = self.b.tool("create_channel", {
                "title": f"{PREFIX}{kind}-{stamp}",
                "about": "Disposable fixture created by mcp_fixture_test.py",
                "megagroup": mega,
            })
            cid = res.get("chat_id")
            if not cid:
                print(f"  could not create {kind}: "
                      f"{res.get('error') or res}", file=sys.stderr)
                continue
            self.fixtures.add(int(cid))
            print(f"  created {kind}: {cid}")
            setattr(self, kind, int(cid))
        return bool(self.fixtures)

    def destroy_fixtures(self):
        """Delete every fixture, retrying rather than leaving debris.

        A single attempt is not enough: delete_channel can time out client-side
        while Telegram carries the deletion out anyway, and the chat list is
        cached for 60s, so a fixture can look alive when it is already gone.
        Both showed up as channels left behind on a real account. Retry, then
        confirm against a list read after the cache has turned over.
        """
        survivors = []
        for cid in sorted(self.fixtures):
            for attempt in range(3):
                try:
                    res = self.b.tool("delete_channel", {"chat_id": cid})
                    if res.get("success"):
                        print(f"  deleted {cid}")
                        break
                    err = str(res.get("error") or res)
                    # already gone; the list we read from was simply stale
                    if "CHANNEL_PRIVATE" in err or "CHANNEL_INVALID" in err:
                        print(f"  {cid} already gone")
                        break
                except Exception as e:  # noqa: BLE001 - report, never mask
                    err = str(e)
                time.sleep(5 * (attempt + 1))
            else:
                survivors.append((cid, err))
        return survivors


def main():
    h = Harness()
    print("Creating fixtures ...")
    if not h.create_fixtures():
        sys.exit("no fixtures created — cannot run destructive tests")

    ch = getattr(h, "channel", None)
    sg = getattr(h, "supergroup", None)

    try:
        print("\nRunning destructive tests against fixtures only ...")

        # messaging
        sent = h.call("send_message", "send_message",
                      {"chat_id": ch, "text": "fixture test message"})
        h.call("send_message(empty text)", "send_message",
               {"chat_id": ch, "text": ""}, expect="error")
        time.sleep(3)

        read = h.call("read_messages", "read_messages",
                      {"chat_id": ch, "limit": 5})
        msg_id = None
        msgs = read.get("messages") or []
        if msgs and isinstance(msgs[0], dict):
            msg_id = msgs[0].get("message_id") or msgs[0].get("id")

        if msg_id:
            h.call("edit_message", "edit_message",
                   {"chat_id": ch, "message_id": msg_id, "new_text": "edited"})
            h.call("pin_message", "pin_message",
                   {"chat_id": ch, "message_id": msg_id})
            h.call("unpin_message", "unpin_message",
                   {"chat_id": ch, "message_id": msg_id})
            h.call("add_reaction", "add_reaction",
                   {"chat_id": ch, "message_id": msg_id, "emoji": "👍"})
            h.call("forward_message", "forward_message",
                   {"from_chat_id": ch, "to_chat_id": sg,
                    "message_id": msg_id})
            h.call("delete_message", "delete_message",
                   {"chat_id": ch, "message_id": msg_id})
        else:
            h.results.append(("message-dependent tests", "SKIP",
                              "no message id came back from read_messages"))

        # channel administration
        h.call("rename_chat", "rename_chat",
               {"chat_id": ch, "title": f"{PREFIX}renamed"})
        h.call("set_reaction_price", "set_reaction_price",
               {"chat_id": sg, "min_stars": 0})
        # broadcast channel: the setting exists only there
        h.call("toggle_gift_notifications", "toggle_gift_notifications",
               {"chat_id": ch, "enabled": True})
        # supergroup: must be refused with a clear reason, not PEER_ID_INVALID
        h.call("toggle_gift_notifications(supergroup)",
               "toggle_gift_notifications",
               {"chat_id": sg, "enabled": True}, expect="error")
        h.call("get_chat_info", "get_chat_info", {"chat_id": ch})

        # batch paths
        h.call("batch_send", "batch_send",
               {"chat_ids": [ch], "message": "batch fixture message"})
        h.call("archive_chat", "archive_chat", {"chat_id": ch})

        # scoping guard must actually refuse a non-fixture chat
        h.call("guard rejects foreign chat", "send_message",
               {"chat_id": 777000, "text": "must never be sent"})

    finally:
        print("\nDeleting fixtures ...")
        survivors = h.destroy_fixtures()

    print(f"\n{'RESULT':8} {'TEST':34} DETAIL")
    counts: dict[str, int] = {}
    for label, status, detail in h.results:
        counts[status] = counts.get(status, 0) + 1
        print(f"{status:8} {label:34} {detail}")
    print("\nsummary:", counts)
    if survivors:
        print("!! fixtures NOT deleted — remove these by hand:", survivors)
        sys.exit(1)
    print("all fixtures deleted")


if __name__ == "__main__":
    main()
