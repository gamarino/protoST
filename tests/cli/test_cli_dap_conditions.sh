#!/usr/bin/env bash
# DAP conditions: conditions on the exception filters (the exception bound as
# `ex`), and line breakpoints with a condition, a hitCondition or a
# logMessage.
#
#   exc_true / exc_false   a raised-filter condition that holds stops only on
#                          the exception it selects; one that never holds lets
#                          the program run to its end
#   exc_error              a condition that fails (it raises) stops, and says so
#   exc_nested             a condition that calls a method with a breakpoint
#                          does not stop on that breakpoint
#   line_*                 condition true / false, hitCondition `3`, `>= 5`,
#                          `% 2` (exactly which iterations stop), an invalid
#                          hitCondition, a condition error
#   log / log_error        a logpoint logs with {expr} and {{ }} and never stops
#   threads                a conditional breakpoint hit by two threads stops
#                          only the thread whose value satisfies it
set -euo pipefail
PROTOST="$1"
HERE="$(cd "$(dirname "$0")" && pwd)"

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

cat > "$WORK/exc.st" <<'EOF'
Object subclass: #Calc.
Calc >> check: n with: other
  n > 2 ifTrue: [ ZeroDivide new signal: 'too big ', n printString ].
  ^ n.
Calc >> helper
  ^ 42.
c := Calc new.
1 to: 5 do: [ :i |
  | r |
  r := [ c check: i with: c ] on: Error do: [ :e | -1 ].
  r printNl ].
'done' displayNl.
EOF

cat > "$WORK/loop.st" <<'EOF'
total := 0.
1 to: 6 do: [ :i |
  total := total + i ].
total printNl.
EOF

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
(f1 wait + f2 wait) printNl.
EOF

cat > "$WORK/driver.py" <<'PYEOF'
import sys
sys.dont_write_bytecode = True   # no __pycache__ in the source tree
sys.path.insert(0, sys.argv[1])
from dap_client import DapClient, DapError

protost, work, case = sys.argv[2], sys.argv[3], sys.argv[4]
sep = "\\" if "\\" in work else "/"
def path(name):
    return work + sep + name

def check(cond, what):
    if not cond:
        print("FAIL:", case, what)
        sys.exit(1)

def run_stops(c, on_stop=None, timeout=10):
    """Continue every stop until the program ends; the list of stop bodies."""
    stops = []
    i = 0
    while True:
        try:
            ev, i = c.wait_event(lambda e: e.get("event") in ("stopped", "terminated"),
                                 timeout=timeout, start=i)
        except DapError:
            check(False, "no stop and no end")
        i += 1
        if ev["event"] == "terminated":
            return stops
        b = ev["body"]
        if on_stop:
            on_stop(b)
        stops.append(b)
        c.request("continue", {"threadId": b["threadId"]})

def frame_locals(c, tid):
    f = c.top_frame(tid)
    return f[0]["line"], c.locals(f[0]["id"])

c = DapClient(protost)
try:
    caps = c.request("initialize", {"adapterID": "protost"})["body"]
    if case == "caps":
        check(caps.get("supportsExceptionFilterOptions") and caps.get("supportsConditionalBreakpoints")
              and caps.get("supportsHitConditionalBreakpoints") and caps.get("supportsLogPoints"),
              "capabilities %r" % caps)
        for f in caps["exceptionBreakpointFilters"]:
            check(f.get("supportsCondition") and "ex" in f.get("conditionDescription", ""),
                  "filter %r" % f)
        print("OK", case)
        sys.exit(0)

    def launch(prog, bps=None, filter_options=None):
        c.request("launch", {"program": prog})
        if filter_options is not None:
            r = c.request("setExceptionBreakpoints", {"filters": [], "filterOptions": filter_options})
            check(r["success"], "setExceptionBreakpoints")
        r = c.request("setBreakpoints", {"source": {"path": prog}, "breakpoints": bps or []})
        c.request("configurationDone")
        return r["body"]["breakpoints"]

    if case.startswith("exc_"):
        prog = path("exc.st")
        cond = {
            "exc_true":   "ex messageText = 'too big 4'",
            "exc_false":  "ex isKindOf: SubscriptOutOfBounds",
            "exc_error":  "ex fooBar",
            "exc_nested": "other helper = 42",
        }[case]
        bps = [{"line": 6}] if case == "exc_nested" else []
        launch(prog, bps, [{"filterId": "raised", "condition": cond}])
        seen = []
        def note(b):
            line, loc = frame_locals(c, b["threadId"])
            seen.append((b["reason"], line, loc.get("n"), b.get("text", "")))
        run_stops(c, note)
        out = c.output_text()
        check("done" in out, "the program ran to its end: %r" % out)
        if case == "exc_true":
            check([(r, l, n) for r, l, n, _ in seen] == [("exception", 3, "4")], "stops %r" % seen)
        elif case == "exc_false":
            check(seen == [], "stops %r" % seen)
        elif case == "exc_error":
            check([n for _, _, n, _ in seen] == ["3", "4", "5"], "stops %r" % seen)
            check(all("condition `ex fooBar` failed" in t for _, _, _, t in seen), "text %r" % seen)
            check("exception breakpoint condition `ex fooBar` failed" in c.output_text("stderr"),
                  "stderr %r" % c.output_text("stderr"))
        else:
            check([(r, n) for r, _, n, _ in seen] == [("exception", "3"), ("exception", "4"), ("exception", "5")],
                  "only exception stops, never the breakpoint in helper: %r" % seen)
    elif case.startswith("line_") or case.startswith("log"):
        prog = path("loop.st")
        bp = {"line": 3}
        bp.update({
            "line_true":    {"condition": "i = 4"},
            "line_false":   {"condition": "i > 10"},
            "line_hit3":    {"hitCondition": "3"},
            "line_hitge":   {"hitCondition": ">= 5"},
            "line_hitmod":  {"hitCondition": "% 2"},
            "line_condhit": {"condition": "i odd", "hitCondition": "2"},
            "line_badhit":  {"hitCondition": "abc"},
            "line_error":   {"condition": "i fooBar"},
            "log":          {"logMessage": "i={i} {{literal}} total={total}"},
            "log_error":    {"logMessage": "bad {i fooBar} end"},
        }[case])
        res = launch(prog, [bp])
        seen = []
        def note(b):
            line, loc = frame_locals(c, b["threadId"])
            seen.append((line, loc.get("i"), b.get("text", "")))
        stops = run_stops(c, note)
        out = c.output_text().replace("\r\n", "\n")
        check(out.endswith("21\n"), "the program ran to its end: %r" % out)
        iters = [i for _, i, _ in seen]
        expect = {
            "line_true": ["4"], "line_false": [], "line_hit3": ["3"], "line_hitge": ["5", "6"],
            "line_hitmod": ["2", "4", "6"], "line_condhit": ["3"], "line_badhit": [],
            "line_error": ["1", "2", "3", "4", "5", "6"], "log": [], "log_error": [],
        }[case]
        check(iters == expect, "stopped at iterations %r, expected %r" % (iters, expect))
        if case == "line_badhit":
            check(res[0]["verified"] is False and "invalid hit condition" in res[0].get("message", ""),
                  "unverified %r" % res)
        if case == "line_error":
            check(all("breakpoint condition `i fooBar` failed" in t for _, _, t in seen), "text %r" % seen)
        console = c.output_text("console").replace("\r\n", "\n")
        if case == "log":
            want = "".join("i=%d {literal} total=%d\n" % (i, sum(range(1, i))) for i in range(1, 7))
            check(console == want, "log %r" % console)
        if case == "log_error":
            lines = console.splitlines()
            check(len(lines) == 6 and all(l.startswith("bad <error: ") and l.endswith("> end") for l in lines),
                  "log %r" % console)
    elif case == "threads":
        prog = path("two.st")
        launch(prog, [{"line": 8, "condition": "k = 7"}])
        seen = []
        def note(b):
            line, loc = frame_locals(c, b["threadId"])
            seen.append((b["threadId"], line, loc.get("k"), loc.get("acc")))
        run_stops(c, note, timeout=30)
        check(len(seen) == 1 and seen[0][1:] == (8, "7", "200") and seen[0][0] != 1, "stops %r" % seen)
        check("312" in c.output_text(), "output")
    print("OK", case)
finally:
    c.kill()
PYEOF

export PROTOST_WORKERS=4
for case in caps exc_true exc_false exc_error exc_nested \
            line_true line_false line_hit3 line_hitge line_hitmod line_condhit \
            line_badhit line_error log log_error threads; do
    "${PYTHON:-python3}" "$WORK/driver.py" "$HERE" "$PROTOST" "$WORK" "$case"
done
echo OK
