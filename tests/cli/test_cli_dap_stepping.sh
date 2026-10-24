#!/usr/bin/env bash
# DAP stepping by source line and call depth: `next` steps over a method call
# and over a block a primitive runs, `stepIn` enters the method, `stepOut`
# returns to the caller; the call stack spans the method and the block engines.
set -euo pipefail
PROTOST="$1"
HERE="$(cd "$(dirname "$0")" && pwd)"

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

cat > "$WORK/step.st" <<'EOF'
Object subclass: #Calc.
Calc >> twice: n
  | r |
  r := n * 2.
  ^ r.
c := Calc new.
a := c twice: 3.
b := a + 1.
x := c twice: b.
#(1 2) do: [ :e | e printNl ].
x printNl.
#(7) do: [ :e | (c twice: e) printNl ].
EOF

cat > "$WORK/driver.py" <<'PYEOF'
import sys
sys.dont_write_bytecode = True   # no __pycache__ in the source tree
sys.path.insert(0, sys.argv[1])
from dap_client import DapClient

protost, prog = sys.argv[2], sys.argv[3]

def check(cond, what):
    if not cond:
        print("FAIL:", what)
        sys.exit(1)

c = DapClient(protost)
try:
    c.start(prog, breakpoints=[7, 4])
    ev, i = c.stopped()
    check(ev["body"]["reason"] == "breakpoint", "breakpoint")
    def where():
        f = c.top_frame(1)
        return f[0]["line"], f[0]["name"], len(f), f
    def step(cmd, reason="step"):
        n = len(c.events)
        c.request(cmd, {"threadId": 1})
        e, _ = c.stopped(start=n)
        check(e["body"]["reason"] == reason, "%s stops for %s: %r" % (cmd, reason, e["body"]))
        return where()
    line, name, depth, _ = where()
    check((line, name, depth) == (7, "<module>", 1), "at line 7: %r" % ((line, name, depth),))
    # `next` over `c twice: 3` -- the breakpoint at line 4 inside it stops first.
    line, name, depth, _ = step("next", "breakpoint")
    check((line, depth) == (4, 2) and "twice:" in name, "breakpoint inside: %r" % ((line, name, depth),))
    c.request("setBreakpoints", {"source": {"path": prog}, "breakpoints": []})
    line, name, depth, _ = step("stepOut")
    check((line, name, depth) == (7, "<module>", 1), "stepOut: %r" % ((line, name, depth),))
    line, name, depth, f = step("next")
    check((line, depth) == (8, 1), "next to line 8: %r" % ((line, name, depth),))
    check(c.locals(f[0]["id"]).get("a") == "6", "a = 6")
    line, name, depth, _ = step("next")
    check((line, depth) == (9, 1), "next to line 9: %r" % ((line, name, depth),))
    line, name, depth, _ = step("stepIn")
    check((line, depth) == (4, 2) and "twice:" in name, "stepIn: %r" % ((line, name, depth),))
    line, name, depth, f = step("next")
    check((line, depth) == (5, 2), "next inside: %r" % ((line, name, depth),))
    check(c.locals(f[0]["id"]).get("r") == "14", "r = 14: %r" % c.locals(f[0]["id"]))
    line, name, depth, _ = step("next")
    check(depth == 1 and line == 9, "next off the method's end returns to the caller: %r" % ((line, name, depth),))
    line, name, depth, _ = step("next")
    check((line, depth) == (10, 1), "line 10: %r" % ((line, name, depth),))
    # `next` over a do: -- the block runs in an engine of its own, deeper.
    line, name, depth, _ = step("next")
    check((line, depth) == (11, 1), "next over the do: block: %r" % ((line, name, depth),))
    line, name, depth, _ = step("next")
    check((line, depth) == (12, 1), "line 12: %r" % ((line, name, depth),))
    # stepIn enters the block, then the method; the stack shows all three.
    line, name, depth, _ = step("stepIn")
    check(depth == 2 and (name.startswith("[]") or name == "<block>"), "in the block: %r" % ((line, name, depth),))
    line, name, depth, f = step("stepIn")
    check(depth == 3 and "twice:" in name, "in the method from the block: %r" % ((line, name, depth),))
    check([fr["line"] for fr in f] == [4, 12, 12], "stack lines %r" % [fr["line"] for fr in f])
    c.request("continue", {"threadId": 1})
    rc = c.finish()
    out = c.output_text().replace("\r\n", "\n")
    check(out == "1\n2\n14\n14\n", "output %r" % out)
    check(rc == 0, "exit %r" % rc)
    print("OK stepping")
finally:
    c.kill()
PYEOF

"${PYTHON:-python3}" "$WORK/driver.py" "$HERE" "$PROTOST" "$WORK/step.st"
echo OK
