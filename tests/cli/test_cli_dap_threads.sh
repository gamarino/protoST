#!/usr/bin/env bash
# DAP multi-thread debugging, one-thread stop.
#
#   1. two: two actors on two worker threads hit the same breakpoint -- two
#      `stopped` events with different threadIds, allThreadsStopped false; the
#      main thread keeps running (its output keeps coming) while both are
#      stopped; `next` in one thread steps that thread only, the other stays
#      stopped where it was; each is continued; the program completes.
#   2. pause: `pause` of the busy main thread stops it inside its loop; an
#      evaluate on it changes the loop's condition; continue ends the loop.
#   3. gc: a worker stays stopped while the main thread allocates far past a
#      hard heap ceiling (so it can only finish if collections run while the
#      worker is stopped); afterwards the stopped worker's locals are intact.
set -euo pipefail
PROTOST="$1"
HERE="$(cd "$(dirname "$0")" && pwd)"

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

cat > "$WORK/two.st" <<'EOF'
Object subclass: #Worker instanceVariableNames: 'id'.
Worker >> setId: n
  id := n.
  ^ self.
Worker >> work: k into: done
  | acc |
  acc := id * 100.
  acc := acc + k.
  done swap: [ :d | d + 1 ].
  ^ acc.
done := Atom on: 0.
w1 := (Worker new setId: 1) asActor.
w2 := (Worker new setId: 2) asActor.
f1 := w1 work: 5 into: done.
f2 := w2 work: 7 into: done.
ticks := 0.
[ done value < 2 ] whileTrue: [
  ticks := ticks + 1.
  (ticks \\ 20000) = 0 ifTrue: [ 'main tick' displayNl ] ].
(f1 wait + f2 wait) printNl.
EOF

cat > "$WORK/pause.st" <<'EOF'
flag := Atom on: 0.
n := 0.
'spinning' displayNl.
[ flag value = 0 ] whileTrue: [
  n := n + 1 ].
'stopped spinning' displayNl.
EOF

cat > "$WORK/gc.st" <<'EOF'
Object subclass: #Holder.
Holder >> hold: k signal: started
  | box label |
  box := Array with: 'kept' with: k with: k * 2.
  label := 'label ', k printString.
  started swap: [ :v | 1 ].
  ^ box size + label size.
started := Atom on: 0.
f := Holder new asActor hold: 21 signal: started.
"Allocate only once the worker is about to stop at its breakpoint."
[ started value = 0 ] whileTrue: [ nil ].
kept := OrderedCollection new.
1 to: 100000 do: [ :i |
  | a |
  a := Array new: 8.
  a at: 1 put: i.
  (i \\ 100) = 0 ifTrue: [
    kept add: a.
    kept size > 1000 ifTrue: [ kept removeFirst ] ] ].
'allocated' displayNl.
f wait printNl.
EOF

cat > "$WORK/driver.py" <<'PYEOF'
import sys, time
sys.path.insert(0, sys.argv[1])
from dap_client import DapClient

protost, work, case = sys.argv[2], sys.argv[3], sys.argv[4]
sep = "\\" if "\\" in work else "/"
def path(name):
    return work + sep + name

def check(cond, what):
    if not cond:
        print("FAIL:", what)
        sys.exit(1)

def wait_for(pred, timeout=60):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if pred():
            return True
        time.sleep(0.05)
    return False

c = DapClient(protost)
try:
    if case == "two":
        prog = path("two.st")
        c.start(prog, breakpoints=[8])
        e1, i1 = c.stopped(timeout=60)
        e2, _ = c.stopped(timeout=60, start=i1 + 1)
        a, b = e1["body"]["threadId"], e2["body"]["threadId"]
        check(a != b, "two different threads %r %r" % (a, b))
        check(1 not in (a, b), "the stops are on worker threads")
        check(e1["body"]["allThreadsStopped"] is False, "allThreadsStopped")
        names = {t["id"]: t["name"] for t in c.request("threads")["body"]["threads"]}
        check(1 in names and a in names and b in names, "threads %r" % names)
        check("actor Worker#" in names[a] and "actor Worker#" in names[b], "names %r" % names)
        started = [e["body"]["threadId"] for e in c.events
                   if e.get("event") == "thread" and e["body"]["reason"] == "started"]
        check(a in started and b in started, "thread started events %r" % started)
        # The main thread keeps running while both workers are stopped.
        before = c.output_text().count("main tick")
        check(wait_for(lambda: c.output_text().count("main tick") >= before + 3),
              "main thread output while two threads are stopped")
        fa = c.top_frame(a)
        fb = c.top_frame(b)
        check(fa[0]["line"] == 8 and fb[0]["line"] == 8, "both at line 8")
        acc_a = int(c.locals(fa[0]["id"])["acc"])
        # Step over in thread a only.
        n_events = len(c.events)
        c.request("next", {"threadId": a})
        es, _ = c.stopped(timeout=30, start=n_events)
        check(es["body"]["threadId"] == a and es["body"]["reason"] == "step",
              "step stop %r" % es["body"])
        fa = c.top_frame(a)
        check(fa[0]["line"] == 9, "stepped to line 9: %r" % fa[0])
        acc_a2 = int(c.locals(fa[0]["id"])["acc"])
        check(acc_a2 - acc_a in (5, 7), "acc after the step %r -> %r" % (acc_a, acc_a2))
        # Thread b did not move.
        fb2 = c.top_frame(b)
        check(fb2[0]["line"] == 8, "the other thread stays at line 8: %r" % fb2[0])
        check(not any(e.get("event") == "stopped" and e["body"]["threadId"] == b
                      for e in c.events[n_events:]), "no new stop of the other thread")
        r = c.request("continue", {"threadId": a})
        check(r["body"]["allThreadsContinued"] is False, "allThreadsContinued")
        c.request("continue", {"threadId": b})
        rc = c.finish(timeout=60)
        check("312" in c.output_text(), "result %r" % c.output_text()[-200:])
        check(rc == 0, "exit %r" % rc)
    elif case == "pause":
        prog = path("pause.st")
        c.start(prog)
        check(wait_for(lambda: "spinning" in c.output_text()), "the loop started")
        time.sleep(0.2)
        n_events = len(c.events)
        r = c.request("pause", {"threadId": 1})
        check(r["success"], "pause response")
        ev, _ = c.stopped(timeout=30, start=n_events)
        check(ev["body"]["reason"] == "pause" and ev["body"]["threadId"] == 1,
              "pause stop %r" % ev["body"])
        frames = c.top_frame(1)
        check(frames[0]["line"] in (4, 5), "paused in the loop: %r" % frames[0])
        loc = c.locals(frames[0]["id"])
        check(int(loc["n"]) > 0, "the loop ran: %r" % loc)
        ev2 = c.evaluate("flag swap: [ :v | 1 ]", frames[0]["id"])
        check(ev2["success"], "evaluate %r" % ev2)
        c.request("continue", {"threadId": 1})
        rc = c.finish(timeout=60)
        check("stopped spinning" in c.output_text(), "loop ended")
        check(rc == 0, "exit %r" % rc)
    elif case == "gc":
        prog = path("gc.st")
        c.start(prog, breakpoints=[7])
        ev, _ = c.stopped(timeout=60)
        tid = ev["body"]["threadId"]
        check(tid != 1, "stopped on a worker %r" % ev)
        # The main thread allocates past the ceiling while the worker is stopped.
        check(wait_for(lambda: "allocated" in c.output_text(), timeout=240),
              "the main thread finished allocating while a thread was stopped")
        frames = c.top_frame(tid)
        loc = c.locals(frames[0]["id"])
        check(loc.get("label") == "'label 21'" or loc.get("label") == "label 21",
              "label %r" % loc)
        e1 = c.evaluate("(box at: 1) , ' ' , (box at: 3) printString", frames[0]["id"])
        check(e1["success"] and "kept 42" in e1["body"]["result"], "box %r" % e1)
        c.request("continue", {"threadId": tid})
        rc = c.finish(timeout=60)
        out = c.output_text()
        check(out.replace("\r\n", "\n").endswith("allocated\n11\n"), "output %r" % out[-100:])
        check(rc == 0, "exit %r" % rc)
    print("OK", case)
finally:
    c.kill()
PYEOF

export PROTOST_WORKERS=4
"${PYTHON:-python3}" "$WORK/driver.py" "$HERE" "$PROTOST" "$WORK" two
"${PYTHON:-python3}" "$WORK/driver.py" "$HERE" "$PROTOST" "$WORK" pause
PROTOCORE_HEAP_LIMIT_CELLS=400000 "${PYTHON:-python3}" "$WORK/driver.py" "$HERE" "$PROTOST" "$WORK" gc
echo OK
