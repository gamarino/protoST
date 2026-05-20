#!/usr/bin/env bash
# F8-3 / F8-4: DAP debug session — launch, breakpoints, stop/resume handshake,
# and inspection (stackTrace / scopes / variables / evaluate).
#
# The DAP exchange is async: events (stopped / terminated) are interleaved with
# request/response traffic and the debuggee runs on its own thread. A purely
# linear pipe of requests cannot work — the next request depends on which event
# arrived. So this test drives the adapter with a small Python harness that
# reads framed DAP messages incrementally and reacts to events (no fixed
# sleeps; the exchange is deterministic).
#
# Covered paths:
#   1. launch -> run-to-completion -> terminated   (no breakpoints)
#   2. setBreakpoints -> stopped(breakpoint) -> continue -> terminated
#   3. F8-4: setBreakpoints -> stopped -> stackTrace / scopes / variables /
#            evaluate -> continue -> terminated
set -euo pipefail
PROTOST="$1"

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

SCRIPT="$WORK/prog.st"
cat > "$SCRIPT" <<'EOF'
x := 1 + 2.
y := x + 10.
y.
EOF

# BL-3: a second script whose top-level variable holds a user-class instance,
# so the DAP `variables` panel can be checked for "a Counter" instead of the
# old "<obj>".
OBJSCRIPT="$WORK/objprog.st"
cat > "$OBJSCRIPT" <<'EOF'
Object subclass: #Counter instanceVariableNames: 'count'.
c := Counter newChild.
z := 99.
z.
EOF

# The Python driver. It speaks framed DAP, reacts to events, and prints a
# summary line beginning with RESULT: that the shell asserts on.
DRIVER="$WORK/driver.py"
cat > "$DRIVER" <<'PYEOF'
import subprocess, sys, time, json

protost, script, mode = sys.argv[1], sys.argv[2], sys.argv[3]

def framed(body):
    b = body.encode()
    return b'Content-Length: %d\r\n\r\n%s' % (len(b), b)

p = subprocess.Popen([protost, "--dap"],
                     stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                     stderr=subprocess.DEVNULL)

def send(m):
    p.stdin.write(framed(m)); p.stdin.flush()

buf = b''
def read_msg():
    global buf
    while b'\r\n\r\n' not in buf:
        c = p.stdout.read(1)
        if not c:
            return None
        buf += c
    hdr, rest = buf.split(b'\r\n\r\n', 1)
    n = 0
    for h in hdr.decode().split('\r\n'):
        if h.lower().startswith('content-length'):
            n = int(h.split(':')[1])
    while len(rest) < n:
        c = p.stdout.read(1)
        if not c:
            return None
        rest += c
    buf = rest[n:]
    return rest[:n].decode()

# A request whose response we want to wait for, by seq. read_until_response
# pumps messages, parking events the caller still cares about is unnecessary
# here because every inspection request is issued while already stopped.
def request(seq, command, args):
    send(json.dumps({"seq": seq, "type": "request",
                     "command": command, "arguments": args}))

def await_response(want_seq):
    deadline = time.time() + 15
    while time.time() < deadline:
        m = read_msg()
        if m is None:
            return None
        o = json.loads(m)
        if o.get("type") == "response" and o.get("request_seq") == want_seq:
            return o
    return None

send('{"seq":1,"type":"request","command":"initialize","arguments":{}}')
send('{"seq":2,"type":"request","command":"launch","arguments":{"program":"%s","stopOnEntry":false}}' % script)

if mode == "objvar":
    # Break at line 4 (`z.`) so the prior assignments to `c` and `z` have run.
    send('{"seq":3,"type":"request","command":"setBreakpoints","arguments":'
         '{"source":{"path":"%s"},"breakpoints":[{"line":4}]}}' % script)
elif mode in ("breakpoint", "inspect"):
    send('{"seq":3,"type":"request","command":"setBreakpoints","arguments":'
         '{"source":{"path":"%s"},"breakpoints":[{"line":2}]}}' % script)
else:
    send('{"seq":3,"type":"request","command":"setBreakpoints","arguments":'
         '{"source":{"path":"%s"},"breakpoints":[]}}' % script)
send('{"seq":4,"type":"request","command":"configurationDone","arguments":{}}')

got_stopped = False
got_terminated = False
inspect_ok = 0
seq = 10
deadline = time.time() + 30
while time.time() < deadline:
    m = read_msg()
    if m is None:
        break
    if '"event":"stopped"' in m and '"reason":"breakpoint"' in m:
        got_stopped = True
        if mode == "objvar":
            # BL-3: an object-valued variable must show "a Counter", not "<obj>".
            st_seq = seq; seq += 1
            request(st_seq, "stackTrace", {"threadId": 1})
            st = await_response(st_seq)
            frames = (st or {}).get("body", {}).get("stackFrames", [])
            frame_id = frames[0]["id"] if frames else None

            sc_seq = seq; seq += 1
            request(sc_seq, "scopes", {"frameId": frame_id})
            sc = await_response(sc_seq)
            scopes = (sc or {}).get("body", {}).get("scopes", [])
            locals_scope = next((s for s in scopes
                                 if s.get("name") == "Locals"), None)
            var_ref = locals_scope["variablesReference"] if locals_scope else 0

            va_seq = seq; seq += 1
            request(va_seq, "variables", {"variablesReference": var_ref})
            va = await_response(va_seq)
            variables = (va or {}).get("body", {}).get("variables", [])
            byname = {v["name"]: v["value"] for v in variables}
            inspect_ok = int(byname.get("c") == "a Counter")
        if mode == "inspect":
            # stackTrace
            st_seq = seq; seq += 1
            request(st_seq, "stackTrace", {"threadId": 1})
            st = await_response(st_seq)
            frames = (st or {}).get("body", {}).get("stackFrames", [])
            ok_st = bool(frames) and frames[0].get("line") == 2
            frame_id = frames[0]["id"] if frames else None

            # scopes
            sc_seq = seq; seq += 1
            request(sc_seq, "scopes", {"frameId": frame_id})
            sc = await_response(sc_seq)
            scopes = (sc or {}).get("body", {}).get("scopes", [])
            locals_scope = next((s for s in scopes
                                 if s.get("name") == "Locals"), None)
            ok_sc = locals_scope is not None
            var_ref = locals_scope["variablesReference"] if ok_sc else 0

            # variables — expect named locals incl. x == 3
            va_seq = seq; seq += 1
            request(va_seq, "variables", {"variablesReference": var_ref})
            va = await_response(va_seq)
            variables = (va or {}).get("body", {}).get("variables", [])
            byname = {v["name"]: v["value"] for v in variables}
            ok_va = "x" in byname and byname["x"] == "3"

            # evaluate — arithmetic and an in-scope variable
            e1_seq = seq; seq += 1
            request(e1_seq, "evaluate",
                    {"expression": "2 + 40", "frameId": frame_id,
                     "context": "repl"})
            e1 = await_response(e1_seq)
            ok_e1 = (e1 or {}).get("success") is True and \
                    (e1 or {}).get("body", {}).get("result") == "42"

            e2_seq = seq; seq += 1
            request(e2_seq, "evaluate",
                    {"expression": "x", "frameId": frame_id,
                     "context": "watch"})
            e2 = await_response(e2_seq)
            ok_e2 = (e2 or {}).get("success") is True and \
                    (e2 or {}).get("body", {}).get("result") == "3"

            inspect_ok = int(ok_st and ok_sc and ok_va and ok_e1 and ok_e2)
        send('{"seq":%d,"type":"request","command":"continue","arguments":{"threadId":1}}' % seq)
        seq += 1
    if '"event":"terminated"' in m:
        got_terminated = True
        send('{"seq":%d,"type":"request","command":"disconnect","arguments":{}}' % seq)
        seq += 1
        break

try:
    rc = p.wait(timeout=10)
except subprocess.TimeoutExpired:
    p.kill()
    rc = -1

print("RESULT: stopped=%d terminated=%d inspect=%d exit=%d" %
      (got_stopped, got_terminated, inspect_ok, rc))
PYEOF

# --- path 1: launch -> terminated (no breakpoint) ----------------------------
out=$(python3 "$DRIVER" "$PROTOST" "$SCRIPT" nobreak)
echo "$out"
echo "$out" | grep -q 'RESULT: stopped=0 terminated=1 inspect=0 exit=0' \
    || { echo "FAIL: run-to-completion path"; exit 1; }

# --- path 2: breakpoint -> stopped -> continue -> terminated -----------------
out=$(python3 "$DRIVER" "$PROTOST" "$SCRIPT" breakpoint)
echo "$out"
echo "$out" | grep -q 'RESULT: stopped=1 terminated=1 inspect=0 exit=0' \
    || { echo "FAIL: breakpoint stop/resume path"; exit 1; }

# --- path 3: F8-4 inspection (stackTrace/scopes/variables/evaluate) ----------
out=$(python3 "$DRIVER" "$PROTOST" "$SCRIPT" inspect)
echo "$out"
echo "$out" | grep -q 'RESULT: stopped=1 terminated=1 inspect=1 exit=0' \
    || { echo "FAIL: inspection path"; exit 1; }

# --- path 4: BL-3 — an object-valued variable shows "a Counter" --------------
out=$(python3 "$DRIVER" "$PROTOST" "$OBJSCRIPT" objvar)
echo "$out"
echo "$out" | grep -q 'RESULT: stopped=1 terminated=1 inspect=1 exit=0' \
    || { echo "FAIL: BL-3 object-variable formatting path"; exit 1; }

echo OK
