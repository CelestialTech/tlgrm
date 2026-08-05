#!/usr/bin/env -S uv run --quiet --script
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Exercise the MCP tools against the running client's live session.

Read-only tools are called for real. Mutating tools are called only with
arguments that must fail validation, so their guard paths are exercised
without touching the account.
"""
import json, socket, sys, time

SOCK, TOKEN = "/tmp/tlgrm_mcp.sock", "/tmp/auth_token"


class Bridge:
    def __init__(self):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.settimeout(60)
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
                raise RuntimeError("bridge closed")
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        return json.loads(line)

    def tool(self, name, args):
        r = self.call("tools/call", {"name": name, "arguments": args})
        if "error" in r:
            return {"_rpc_error": r["error"]}
        c = r.get("result", {}).get("content")
        if isinstance(c, list) and c and "text" in c[0]:
            try:
                return json.loads(c[0]["text"])
            except json.JSONDecodeError:
                return {"_raw": c[0]["text"][:300]}
        return r.get("result", {})


def show(label, res, keys=None):
    if "_rpc_error" in res:
        print(f"  {label:32} RPC-ERROR {str(res['_rpc_error'])[:90]}")
        return
    ok = res.get("success")
    err = res.get("error") or res.get("message")
    backing = res.get("backing", "-")
    if err:
        print(f"  {label:32} error[{backing}]: {str(err)[:80]}")
    else:
        extra = ""
        if keys:
            extra = " ".join(f"{k}={json.dumps(res.get(k))[:40]}" for k in keys if k in res)
        print(f"  {label:32} ok={ok} [{backing}] {extra}")


b = Bridge()

print("=== tools/list: backing annotations ===")
lst = b.call("tools/list").get("result", {}).get("tools", [])
print(f"  advertised: {len(lst)}")
from collections import Counter
print(" ", dict(Counter(t.get("backing", "?") for t in lst)))
sample = next((t for t in lst if t.get("backing") == "local-only"), None)
if sample:
    print(f"  sample local-only description tail: ...{sample['description'][-70:]}")

print("\n=== READ-ONLY, real calls ===")
show("get_stars_rate", b.tool("get_stars_rate", {}),
     ["usd_per_star_withdraw", "usd_per_star_sell", "source"])
show("convert_stars(1000)", b.tool("convert_stars", {"stars_amount": 1000}),
     ["usd_if_withdrawn", "usd_if_sold"])
show("get_topup_options", b.tool("get_topup_options", {}), ["count"])
show("get_giveaway_options", b.tool("get_giveaway_options", {}), ["count"])
show("list_achievements", b.tool("list_achievements", {}), ["count"])
show("get_monetization_analytics(self)", b.tool("get_monetization_analytics", {}),
     ["usd_rate", "withdrawal_enabled"])
show("list_chats", b.tool("list_chats", {}), ["count"])

print("\n=== VALIDATION PATHS of mutating tools (no side effects) ===")
show("update_profile_username()", b.tool("update_profile_username", {}))
show("set_reaction_price(no chat)", b.tool("set_reaction_price", {}))
show("update_listing(no id)", b.tool("update_listing", {}))
show("update_gift_display(empty)", b.tool("update_gift_display", {}))
show("reorder_profile_gifts(empty)", b.tool("reorder_profile_gifts", {}))
show("share_achievement(no id)", b.tool("share_achievement", {}))
show("update_profile_phone(no phone)", b.tool("update_profile_phone", {}))
show("toggle_gift_notifications()", b.tool("toggle_gift_notifications", {}))
show("send_document(missing file)", b.tool("send_document", {"chat_id": 1, "file_path": "/nope"}))
show("get_upgrade_options(no id)", b.tool("get_upgrade_options", {}))
show("get_gift_details(no slug)", b.tool("get_gift_details", {}))
show("set_budget_alert(bad pct)", b.tool("set_budget_alert", {"threshold": 150}))
show("set_monetization_rules(none)", b.tool("set_monetization_rules", {}))

print("\n=== LOCAL WRITE round-trip (safe, local db only) ===")
show("set_budget_alert(25%)", b.tool("set_budget_alert", {"threshold": 25, "type": "percentage"}),
     ["id", "threshold", "alert_type"])
show("set_monetization_rules", b.tool("set_monetization_rules", {"rules": {"min_stars": 5}}), ["rules"])

print("\n=== unknown tool ===")
show("definitely_not_a_tool", b.tool("definitely_not_a_tool", {}))
