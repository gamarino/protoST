#!/usr/bin/env bash
# S4 regression: a DAP debug session while the debuggee's actors allocate.
#
# The debuggee runs a program whose eight actors allocate garbage while the
# debuggee waits on their futures. In the `breakpoint` mode the debuggee stops
# while the actors are still allocating, and the adapter evaluates expressions
# (which allocate on the adapter thread) before continuing.
#
# Before S4 the debuggee ran on a plain std::thread that shared the adapter
# thread's ProtoContext; the adapter's read counted that context as parked
# while the debuggee mutated the heap through it, and a wait counted it twice.
# The session must terminate normally, with no error output, every time. The
# evaluate-while-stopped corruption does not need a collection to show up; the
# test runs with the default protoCore configuration and does not force one.
set -euo pipefail
PROTOST="$1"

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

SCRIPT="$WORK/churn.st"
cat > "$SCRIPT" <<'EOF'
Object subclass: #Churn instanceVariableNames: 'id'.
Churn >> setId: n  id := n. ^ self.
Churn >> churn: rounds
  | a |
  1 to: rounds do: [ :i | a := Array new: 100 ].
  ^ id.
workers := OrderedCollection new.
1 to: 8 do: [ :i | workers add: ((Churn new setId: i) asActor) ].
pending := Set new.
workers do: [ :w | pending add: (w churn: 2000) ].
marker := 1.
sum := 0.
pending do: [ :f | sum := sum + f wait ].
sum = 36 ifFalse: [ Error signal: 'wrong sum' ].
sum.
EOF

DRIVER="$WORK/driver.py"
cat > "$DRIVER" <<'PYEOF'
import json, subprocess, sys, time

protost, script, mode = sys.argv[1], sys.argv[2], sys.argv[3]

p = subprocess.Popen([protost, "--dap"], stdin=subprocess.PIPE,
                     stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)

def send(obj):
    body = json.dumps(obj).encode()
    p.stdin.write(b"Content-Length: %d\r\n\r\n%s" % (len(body), body))
    p.stdin.flush()

buf = b""
def read_msg():
    global buf
    while b"\r\n\r\n" not in buf:
        c = p.stdout.read(1)
        if not c:
            return None
        buf += c
    hdr, rest = buf.split(b"\r\n\r\n", 1)
    n = 0
    for h in hdr.decode().split("\r\n"):
        if h.lower().startswith("content-length"):
            n = int(h.split(":")[1])
    while len(rest) < n:
        c = p.stdout.read(1)
        if not c:
            return None
        rest += c
    buf = rest[n:]
    return json.loads(rest[:n].decode())

seq = 1
def request(command, args):
    global seq
    send({"seq": seq, "type": "request", "command": command, "arguments": args})
    seq += 1
    return seq - 1

request("initialize", {})
request("launch", {"program": script, "stopOnEntry": False})
lines = [11] if mode == "breakpoint" else []
request("setBreakpoints", {"source": {"path": script},
                           "breakpoints": [{"line": l} for l in lines]})
request("configurationDone", {})

stopped = terminated = evaluated = 0
errors = []
pending_evals = set()
deadline = time.time() + 60
while time.time() < deadline:
    m = read_msg()
    if m is None:
        break
    if m.get("type") == "event" and m.get("event") == "output":
        if m.get("body", {}).get("category") == "stderr":
            errors.append(m["body"].get("output", ""))
    if m.get("type") == "event" and m.get("event") == "stopped":
        stopped += 1
        for _ in range(5):
            pending_evals.add(request("evaluate",
                {"expression": "(Array new: 200) size", "context": "repl"}))
    if m.get("type") == "response" and m.get("request_seq") in pending_evals:
        pending_evals.discard(m["request_seq"])
        if m.get("success") and m.get("body", {}).get("result") == "200":
            evaluated += 1
        if not pending_evals:
            request("continue", {"threadId": 1})
    if m.get("type") == "event" and m.get("event") == "terminated":
        terminated = 1
        request("disconnect", {})
        break

try:
    rc = p.wait(timeout=20)
except subprocess.TimeoutExpired:
    p.kill()
    rc = -1

print("RESULT: stopped=%d evaluated=%d terminated=%d errors=%d exit=%d"
      % (stopped, evaluated, terminated, len(errors), rc))
for e in errors:
    print("stderr:", e.strip())
PYEOF

export PROTOST_WORKERS=8
RUNS=5
for i in $(seq 1 "$RUNS"); do
    out=$(timeout 90 python3 "$DRIVER" "$PROTOST" "$SCRIPT" run) || true
    case "$out" in
        *"RESULT: stopped=0 evaluated=0 terminated=1 errors=0 exit=0"*) ;;
        *) echo "FAIL: run-to-completion session $i/$RUNS: $out"; exit 1 ;;
    esac
    out=$(timeout 90 python3 "$DRIVER" "$PROTOST" "$SCRIPT" breakpoint) || true
    case "$out" in
        *"RESULT: stopped=1 evaluated=5 terminated=1 errors=0 exit=0"*) ;;
        *) echo "FAIL: breakpoint session $i/$RUNS: $out"; exit 1 ;;
    esac
done

echo "OK ($RUNS run-to-completion and $RUNS breakpoint sessions while actors allocate)"
