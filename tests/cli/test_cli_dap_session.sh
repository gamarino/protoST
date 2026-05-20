#!/usr/bin/env bash
# F8-3: DAP debug session — launch, breakpoints, stop/resume handshake.
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

# The Python driver. It speaks framed DAP, reacts to events, and prints a
# summary line beginning with RESULT: that the shell asserts on.
DRIVER="$WORK/driver.py"
cat > "$DRIVER" <<'PYEOF'
import subprocess, sys, time

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

send('{"seq":1,"type":"request","command":"initialize","arguments":{}}')
send('{"seq":2,"type":"request","command":"launch","arguments":{"program":"%s","stopOnEntry":false}}' % script)

if mode == "breakpoint":
    send('{"seq":3,"type":"request","command":"setBreakpoints","arguments":'
         '{"source":{"path":"%s"},"breakpoints":[{"line":2}]}}' % script)
else:
    send('{"seq":3,"type":"request","command":"setBreakpoints","arguments":'
         '{"source":{"path":"%s"},"breakpoints":[]}}' % script)
send('{"seq":4,"type":"request","command":"configurationDone","arguments":{}}')

got_stopped = False
got_terminated = False
seq = 10
deadline = time.time() + 30
while time.time() < deadline:
    m = read_msg()
    if m is None:
        break
    if '"event":"stopped"' in m and '"reason":"breakpoint"' in m:
        got_stopped = True
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

print("RESULT: stopped=%d terminated=%d exit=%d" % (got_stopped, got_terminated, rc))
PYEOF

# --- path 1: launch -> terminated (no breakpoint) ----------------------------
out=$(python3 "$DRIVER" "$PROTOST" "$SCRIPT" nobreak)
echo "$out"
echo "$out" | grep -q 'RESULT: stopped=0 terminated=1 exit=0' \
    || { echo "FAIL: run-to-completion path"; exit 1; }

# --- path 2: breakpoint -> stopped -> continue -> terminated -----------------
out=$(python3 "$DRIVER" "$PROTOST" "$SCRIPT" breakpoint)
echo "$out"
echo "$out" | grep -q 'RESULT: stopped=1 terminated=1 exit=0' \
    || { echo "FAIL: breakpoint stop/resume path"; exit 1; }

echo OK
