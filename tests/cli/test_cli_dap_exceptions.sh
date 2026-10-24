#!/usr/bin/env bash
# DAP exception breakpoints: the `raised` and `uncaught` filters.
#
#   1. raised: stops at a user `signal:` inside a method, before any handler
#      runs, with the right line and locals; exceptionInfo describes it; an
#      expression over the frame's locals evaluates; continue reaches the
#      on:do: handler and the program finishes normally.
#   2. raised: protoST's internal control flow never stops -- a block
#      non-local return (also out of an on:do: and an ensure:), ensure:,
#      loops, inject:into:, detect:ifNone:, an exception resumed by its
#      handler is the only stop.
#   3. no filter: no exception stop at all.
#   4. uncaught: no stop for a caught exception; a stop with the stack intact
#      for one no handler takes (an Error signalled in a method, and a
#      primitive's error at the top level), after which the program ends with
#      the error.
set -euo pipefail
PROTOST="$1"
HERE="$(cd "$(dirname "$0")" && pwd)"

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

cat > "$WORK/raise.st" <<'EOF'
Object subclass: #Calc.
Calc >> risky: n
  | doubled |
  doubled := n * 2.
  Error signal: 'too big: ', doubled printString.
  ^ doubled.
c := Calc new.
r := [ c risky: 4 ] on: Error do: [ :e | 'caught ', e messageText ].
r displayNl.
'done' displayNl.
EOF

cat > "$WORK/control.st" <<'EOF'
Object subclass: #Finder.
Finder >> firstOver: limit in: coll
  coll do: [ :x | x > limit ifTrue: [ ^ x ] ].
  ^ nil.
Finder >> escapeHandler
  ^ [ #(1 2 3) do: [ :x | x = 2 ifTrue: [ ^ x * 100 ] ]. 0 ] on: Error do: [ :e | -1 ].
Finder >> escapeEnsure
  ^ [ #(5 6) do: [ :x | ^ x ] ] ensure: [ 'ensured' displayNl ].
f := Finder new.
(f firstOver: 2 in: #(1 2 3 4)) printNl.
f escapeHandler printNl.
f escapeEnsure printNl.
sum := 0.
1 to: 10 do: [ :i | sum := sum + i ].
[ sum < 100 ] whileTrue: [ sum := sum * 2 ].
sum printNl.
(#(1 2 3) inject: 0 into: [ :a :b | a + b ]) printNl.
(#(1 2 3) detect: [ :x | x > 5 ] ifNone: [ 'none' ]) displayNl.
w := [ Warning signal: 'careful'. 'resumed' ] on: Warning do: [ :e | e resume: nil ].
w displayNl.
'done' displayNl.
EOF

# Two programs that differ in their uncaught exception: an Error signalled in a
# method (line 10), a primitive's error at the top level (line 10).
cat > "$WORK/uncaught1.st" <<'EOF'
Object subclass: #Calc.
Calc >> risky: n
  | doubled |
  doubled := n * 2.
  Error signal: 'boom ', doubled printString.
  ^ doubled.
c := Calc new.
r := [ c risky: 1 ] on: Error do: [ :e | 'caught' ].
r displayNl.
c risky: 21.
'not reached' displayNl.
EOF
cat > "$WORK/uncaught2.st" <<'EOF'
Object subclass: #Calc.
Calc >> risky: n
  | doubled |
  doubled := n * 2.
  Error signal: 'boom ', doubled printString.
  ^ doubled.
c := Calc new.
r := [ c risky: 1 ] on: Error do: [ :e | 'caught' ].
r displayNl.
#(1 2) at: 5.
'not reached' displayNl.
EOF

cat > "$WORK/driver.py" <<'PYEOF'
import sys
sys.dont_write_bytecode = True   # no __pycache__ in the source tree
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

def exception_stops(c):
    return [e for e in c.events if e.get("event") == "stopped"
            and e["body"].get("reason") == "exception"]

c = DapClient(protost)
try:
    if case == "raised":
        prog = path("raise.st")
        c.start(prog, exception_filters=["raised"])
        ev, _ = c.stopped()
        b = ev["body"]
        check(b["reason"] == "exception", "stop reason %r" % b)
        check(b["allThreadsStopped"] is False, "allThreadsStopped")
        check("too big: 8" in b.get("text", ""), "stopped text %r" % b)
        tid = b["threadId"]
        frames = c.top_frame(tid)
        check(frames[0]["line"] == 5, "frame line %r" % frames[0])
        check("risky:" in frames[0]["name"], "frame name %r" % frames[0])
        loc = c.locals(frames[0]["id"])
        check(loc.get("n") == "4" and loc.get("doubled") == "8", "locals %r" % loc)
        info = c.request("exceptionInfo", {"threadId": tid})
        check(info["success"], "exceptionInfo")
        ib = info["body"]
        check(ib["exceptionId"] == "Error" and ib["breakMode"] == "always", "exceptionInfo %r" % ib)
        check(ib["details"]["typeName"] == "Error" and "too big: 8" in ib["details"]["message"],
              "exceptionInfo details %r" % ib)
        ev2 = c.evaluate("doubled + n", frames[0]["id"])
        check(ev2["success"] and ev2["body"]["result"] == "12", "evaluate %r" % ev2)
        c.request("continue", {"threadId": tid})
        rc = c.finish()
        out = c.output_text()
        check("caught too big: 8" in out and "done" in out, "output %r" % out)
        check(len(exception_stops(c)) == 1, "one exception stop")
        check(rc == 0, "exit %r" % rc)
    elif case == "control":
        prog = path("control.st")
        c.start(prog, exception_filters=["raised"])
        # The only signal is the Warning; continue it and expect no other stop.
        ev, _ = c.stopped()
        check("Warning" in ev["body"].get("text", ""), "warning stop %r" % ev["body"])
        c.request("continue", {"threadId": ev["body"]["threadId"]})
        rc = c.finish()
        out = c.output_text()
        want = "3\n200\nensured\n5\n110\n6\nnone\nresumed\ndone\n"
        check(out.replace("\r\n", "\n") == want, "output %r" % out)
        check(len(exception_stops(c)) == 1, "stops %r" % exception_stops(c))
        check(rc == 0, "exit %r" % rc)
    elif case == "off":
        prog = path("raise.st")
        c.start(prog, exception_filters=[])
        rc = c.finish()
        check(not any(e.get("event") == "stopped" for e in c.events), "a stop with no filter")
        check("caught too big: 8" in c.output_text(), "output")
        check(rc == 0, "exit %r" % rc)
    elif case in ("uncaught1", "uncaught2"):
        prog = path(case + ".st")
        c.start(prog, exception_filters=["uncaught"])
        ev, _ = c.stopped()
        b = ev["body"]
        tid = b["threadId"]
        frames = c.top_frame(tid)
        info = c.request("exceptionInfo", {"threadId": tid})["body"]
        check(info["breakMode"] == "unhandled", "breakMode %r" % info)
        if case == "uncaught1":
            check("boom 42" in b.get("text", ""), "text %r" % b)
            check(frames[0]["line"] == 5 and frames[1]["line"] == 10,
                  "frames %r" % frames[:2])
            check(c.locals(frames[0]["id"]).get("doubled") == "42", "locals")
        else:
            check(info["exceptionId"] == "SubscriptOutOfBounds", "exceptionId %r" % info)
            check("primitive" in b.get("text", ""), "primitive note %r" % b)
            check(frames[0]["line"] == 10, "frame %r" % frames[0])
        # The caught exception before it did not stop.
        check("caught" in c.output_text(), "caught first")
        c.request("continue", {"threadId": tid})
        rc = c.finish()
        check(len(exception_stops(c)) == 1, "one stop")
        check("not reached" not in c.output_text(), "ran past the error")
        exited = [e for e in c.events if e.get("event") == "exited"]
        check(exited and exited[0]["body"]["exitCode"] == 1, "exited %r" % exited)
        check(rc == 0, "adapter exit %r" % rc)
    print("OK", case)
finally:
    c.kill()
PYEOF

for case in raised control off uncaught1 uncaught2; do
    "${PYTHON:-python3}" "$WORK/driver.py" "$HERE" "$PROTOST" "$WORK" "$case"
done
echo OK
