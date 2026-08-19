import json, socket, sys, time
SOCK="/tmp/telebox_qa.sock"
s=socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.settimeout(15); s.connect(SOCK)
buf=b""
def cmd(obj):
    global buf
    s.sendall((json.dumps(obj)+"\n").encode())
    while b"\n" not in buf:
        c=s.recv(1<<20)
        if not c: raise RuntimeError("closed")
        buf+=c
    line,buf=buf.split(b"\n",1)
    return json.loads(line)
ok=True
def check(name, cond, got):
    global ok
    print(("PASS" if cond else "FAIL"), name, "->", got)
    ok = ok and cond
snap=cmd({"cmd":"snapshot"})
check("initial running", snap["running"]==True, snap["host"])
check("initial view plugins", snap["view"]=="plugins", snap["view"])
check("Export active", snap["plugins"][1]["active"]==True, snap["plugins"][1])
check("Wallet inactive", snap["plugins"][5]["active"]==False, snap["plugins"][5])
r=cmd({"cmd":"stop"}); check("after stop -> stopped", r["running"]==False, r["host"])
r=cmd({"cmd":"start"}); check("after start -> running", r["running"]==True, r["host"])
r=cmd({"cmd":"toggle","i":1}); check("toggle Export off", r["plugins"][1]["active"]==False, r["plugins"][1])
r=cmd({"cmd":"toggle","i":0}); check("toggle MCP (i=0) stops host", r["running"]==False, r["host"])
r=cmd({"cmd":"toggle","i":0}); check("toggle MCP again starts host", r["running"]==True, r["host"])
r=cmd({"cmd":"view","name":"permissions"}); check("view->permissions", r["view"]=="permissions", r["view"])
shot=cmd({"cmd":"shot","path":"/tmp/telebox_qa_shot.png"})
check("shot ok", shot.get("ok")==True, shot)
print("shot dims:", shot.get("w"), "x", shot.get("h"))
# restore a nice state for the screenshot: plugins view, Export back on
cmd({"cmd":"toggle","i":1}); cmd({"cmd":"view","name":"plugins"})
shot2=cmd({"cmd":"shot","path":"/tmp/telebox_qa_shot2.png"})
check("shot2 ok", shot2.get("ok")==True, shot2)
print("RESULT:", "ALL PASS" if ok else "SOME FAILED")
sys.exit(0 if ok else 1)
