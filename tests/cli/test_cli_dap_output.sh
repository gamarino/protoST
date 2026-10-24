#!/usr/bin/env bash
# The debuggee's standard streams under `protost --dap`. stdout IS the protocol
# channel, so what the script prints must reach the client as `output` events,
# never as raw bytes between frames; and a script reading stdin must see end of
# file instead of eating the client's requests.
#
# The script prints before and after a breakpoint, writes more than a pipe
# buffer of non-ASCII text, and reads stdin. The driver fails on any byte that
# is not part of a Content-Length frame.
set -euo pipefail
PROTOST="$1"

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

SCRIPT="$WORK/prog.st"
cat > "$SCRIPT" <<'EOF'
| n |
'before' displayNl.
n := 0.
1 to: 5000 do: [:i | 'ñandú línea' displayNl].
Stdio stdin linesDo: [:line | n := n + 1].
('lines read: ', n printString) displayNl.
'after' displayNl.
EOF

DRIVER="$WORK/driver.py"
cat > "$DRIVER" <<'PYEOF'
import json, subprocess, sys, threading

protost, script = sys.argv[1], sys.argv[2]
p = subprocess.Popen([protost, "--dap"], stdin=subprocess.PIPE,
                     stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
# A broken adapter hangs rather than fails (the script swallows the requests):
# kill it, and the read below sees end of file.
watchdog = threading.Timer(60, p.kill)
watchdog.start()
seq = 0

def send(command, arguments):
    global seq
    seq += 1
    b = json.dumps({"seq": seq, "type": "request", "command": command,
                    "arguments": arguments}).encode()
    p.stdin.write(b"Content-Length: %d\r\n\r\n" % len(b) + b)
    p.stdin.flush()

def recv():
    header = b""
    while not header.endswith(b"\r\n\r\n"):
        c = p.stdout.read(1)
        if not c:
            return None
        header += c
    if not header.startswith(b"Content-Length: "):
        print("RESULT: unframed bytes %r" % header[:40])
        sys.exit(0)
    n = int(header[len(b"Content-Length: "):].split(b"\r\n")[0])
    return json.loads(p.stdout.read(n).decode("utf-8"))

send("initialize", {"adapterID": "protost"})
send("launch", {"program": script})
send("setBreakpoints", {"source": {"path": script}, "breakpoints": [{"line": 7}]})
send("configurationDone", {})

output, stopped, before_at_stop, terminated = "", 0, 0, 0
while True:
    m = recv()
    if m is None:
        break
    if m.get("event") == "output" and m["body"].get("category") == "stdout":
        output += m["body"]["output"]
    elif m.get("event") == "stopped":
        stopped += 1
        send("continue", {"threadId": 1})
    elif m.get("event") == "terminated":
        terminated = 1
        send("disconnect", {})
        break

try:
    rc = p.wait(timeout=10)
except subprocess.TimeoutExpired:
    p.kill()
    rc = -1
watchdog.cancel()

lines = output.splitlines()
ok = (lines[:1] == ["before"] and lines.count("ñandú línea") == 5000
      and lines[-2:] == ["lines read: 0", "after"])
print("RESULT: stopped=%d terminated=%d output=%d exit=%d" % (stopped, terminated, int(ok), rc))
PYEOF

out=$("${PYTHON:-python3}" "$DRIVER" "$PROTOST" "$SCRIPT")
echo "$out"
grep -q 'RESULT: stopped=1 terminated=1 output=1 exit=0' <<< "$out" \
    || { echo "FAIL: debuggee output not delivered as DAP output events"; exit 1; }

echo OK
