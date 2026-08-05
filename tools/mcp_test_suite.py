#!/usr/bin/env -S uv run --quiet --script
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Call every advertised MCP tool and classify what comes back.

Runs against a live account, so anything whose name suggests it destroys,
spends, sends or revokes is skipped rather than trusted to validate its way
out of trouble. Everything else is called with no arguments: a tool with
required arguments must refuse, and a tool without them must answer honestly.

What this is looking for:
  LIES      success=true from a tool that cannot have done anything
  CRASH     no reply, or the bridge dropping the connection
  SLOW      a reply that took long enough to suggest a missing timeout
  SCHEMA    required arguments declared but not enforced
"""
import json, socket, sys, time
from collections import Counter, defaultdict

SOCK, TOKEN = "/tmp/tdesktop_mcp.sock", "/tmp/auth_token"

# Substrings that mark a tool as unsafe to fire blind at a real account.
DESTRUCTIVE = (
    "delete", "purge", "remove", "clear", "reset", "revoke", "terminate",
    "block", "leave", "cancel", "stop", "buy", "send", "transfer", "withdraw",
    "pay", "spend", "claim", "bid", "gift", "craft", "upgrade", "convert",
    "archive", "export", "logout", "kick", "ban", "mute", "unpin", "pin",
    "create", "start", "resume", "pause", "update", "set_", "configure",
    "toggle", "edit", "apply", "approve", "share", "forward", "reorder",
    "delist", "list_gift", "refund", "boost", "join", "invite", "restrict",
)


class Bridge:
    def __init__(self):
        self.connect()

    def connect(self):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.settimeout(45)
        self.s.connect(SOCK)
        self.buf = b""
        self.n = 0
        self.call("initialize", {"auth_token": open(TOKEN).read().strip()})

    def call(self, method, params=None):
        self.n += 1
        req = {"jsonrpc": "2.0", "id": self.n, "method": method}
        if params is not None:
            req["params"] = params
        self.s.sendall(json.dumps(req).encode() + b"\n")
        while b"\n" not in self.buf:
            chunk = self.s.recv(1 << 20)
            if not chunk:
                raise ConnectionError("bridge closed")
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        return json.loads(line)

    def tool(self, name, args):
        r = self.call("tools/call", {"name": name, "arguments": args})
        c = r.get("result", {}).get("content")
        if isinstance(c, list) and c and "text" in c[0]:
            try:
                return json.loads(c[0]["text"])
            except json.JSONDecodeError:
                return {"_raw": c[0]["text"][:200]}
        return r.get("result", r)


def unsafe(name):
    return any(d in name for d in DESTRUCTIVE)


b = Bridge()
tools = b.call("tools/list")["result"]["tools"]
print(f"advertised: {len(tools)}  unique: {len(set(t['name'] for t in tools))}")

by_backing = Counter(t.get("backing", "?") for t in tools)
print("backing:", dict(by_backing))

skipped, results = [], []
for t in tools:
    name = t["name"]
    required = (t.get("inputSchema") or {}).get("required") or []
    if unsafe(name):
        skipped.append(name)
        continue
    t0 = time.monotonic()
    try:
        res = b.tool(name, {})
        dt = time.monotonic() - t0
    except (ConnectionError, socket.timeout) as e:
        results.append((name, "CRASH", str(e), 0, required, t.get("backing")))
        try:
            b.connect()
        except Exception:
            print("bridge unrecoverable after", name)
            break
        continue
    ok = res.get("success")
    err = res.get("error") or res.get("message")
    if err:
        kind = "REFUSED"
    elif ok is True:
        kind = "OK_TRUE"
    else:
        kind = "OK_OTHER"
    results.append((name, kind, str(err or "")[:60], dt, required, t.get("backing")))

print(f"\ncalled: {len(results)}   skipped as destructive: {len(skipped)}")
kinds = Counter(k for _, k, _, _, _, _ in results)
print("outcomes:", dict(kinds))

slow = [(n, d) for n, _, _, d, _, _ in results if d > 3.0]
if slow:
    print(f"\nSLOW (>3s), possible missing timeout:")
    for n, d in sorted(slow, key=lambda x: -x[1])[:10]:
        print(f"   {n:38} {d:.1f}s")

crash = [n for n, k, _, _, _, _ in results if k == "CRASH"]
if crash:
    print(f"\nCRASH ({len(crash)}):", crash)

# required-args declared but the tool answered anyway with none supplied
schema_holes = [(n, req, bk) for n, k, _, _, req, bk in results
                if req and k in ("OK_TRUE", "OK_OTHER")]
print(f"\nSCHEMA HOLES — declares required args but answered with none ({len(schema_holes)}):")
for n, req, bk in schema_holes[:25]:
    print(f"   {n:38} requires={req} [{bk}]")

# success=true from a tool that reached nothing
suspicious = [(n, bk) for n, k, _, _, req, bk in results
              if k == "OK_TRUE" and bk in ("unimplemented",)]
print(f"\nUNIMPLEMENTED yet success=true: {suspicious or 'none'}")

json.dump([{"name": n, "kind": k, "err": e, "secs": d, "required": r, "backing": bk}
           for n, k, e, d, r, bk in results],
          open("sweep_results.json", "w"), indent=1)
print("\nwrote sweep_results.json")
