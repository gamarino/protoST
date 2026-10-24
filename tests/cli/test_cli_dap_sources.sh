#!/usr/bin/env bash
# DAP sources: pathMappings, sourceReference, `source`, loadedSources.
#
#   mapped    the client works on a fake Windows tree (c:\fake\proj) mapped to
#             the real test directory: launch.program / cwd and breakpoints by
#             fake path (any case, any separator), a breakpoint in a module
#             imported later (verified by a `breakpoint` event when it loads),
#             stack frames in fake paths; the kernel, under no mapping, comes
#             back by sourceReference -- `source` serves the file's text and a
#             breakpoint on the reference stops; loadedSources lists the
#             program, the import and the kernel
#   unmapped  without pathMappings a kernel frame keeps its path (and is
#             deemphasized)
#   invalid   a pathMappings entry without remoteRoot is a launch error
set -euo pipefail
PROTOST="$1"
HERE="$(cd "$(dirname "$0")" && pwd)"

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/proj"

cat > "$WORK/proj/main.st" <<'EOF'
geo := Import from: '_geom'.
r := geo Rect new.
r setW: 4 h: 6.
a := r area.
a printNl.
(1 to: 3) size printNl.
(1 to: 5) size printNl.
'done' displayNl.
EOF

cat > "$WORK/proj/_geom.st" <<'EOF'
Object subclass: #Rect instanceVariableNames: 'w h'.
Rect >> setW: x h: y
  w := x.
  h := y.
  ^ self.
Rect >> area
  ^ w * h.
EOF

cat > "$WORK/driver.py" <<'PYEOF'
import os, sys
sys.dont_write_bytecode = True   # no __pycache__ in the source tree
sys.path.insert(0, sys.argv[1])
from dap_client import DapClient

protost, proj, case = sys.argv[2], sys.argv[3], sys.argv[4]

def check(cond, what):
    if not cond:
        print("FAIL:", case, what)
        sys.exit(1)

def stop(c, start):
    ev, i = c.stopped(start=start)
    return ev["body"], i + 1

FAKE = "c:\\fake\\proj"
c = DapClient(protost)
try:
    c.request("initialize", {"adapterID": "protost"})
    if case == "invalid":
        r = c.request("launch", {"program": "x.st", "pathMappings": [{"localRoot": "c:\\x"}]})
        check(not r["success"] and "remoteRoot" in r.get("message", ""), "error %r" % r)
        print("OK", case)
        sys.exit(0)
    if case == "unmapped":
        prog = os.path.join(proj, "main.st")
        c.request("launch", {"program": prog, "cwd": proj})
        c.request("setBreakpoints", {"source": {"path": prog}, "breakpoints": [{"line": 6}]})
        c.request("configurationDone")
        b, i = stop(c, 0)
        c.request("stepIn", {"threadId": 1})
        b, i = stop(c, i)
        f = c.top_frame(1)
        src = f[0]["source"]
        check(f[0]["name"] == "Integer>>to:", "frame %r" % f[0])
        check(src.get("path", "").replace("\\", "/").endswith("kernel/collection.st")
              and "sourceReference" not in src and src.get("presentationHint") == "deemphasize",
              "kernel source %r" % src)
        c.request("continue", {"threadId": 1})
        check(c.finish() == 0, "exit")
        print("OK", case)
        sys.exit(0)

    # --- mapped ---
    r = c.request("launch", {"program": "C:\\Fake\\Proj\\main.st", "cwd": "c:/FAKE/proj",
                             "pathMappings": [{"localRoot": FAKE, "remoteRoot": proj}]})
    check(r["success"], "launch %r" % r)
    rg = c.request("setBreakpoints", {"source": {"path": "C:/FAKE/PROJ/_geom.st"},
                                      "breakpoints": [{"line": 7}]})["body"]["breakpoints"]
    check(rg[0]["verified"] is False and rg[0]["id"] > 0, "not loaded yet: %r" % rg)
    rm = c.request("setBreakpoints", {"source": {"path": FAKE + "\\main.st"},
                                      "breakpoints": [{"line": 6}]})["body"]["breakpoints"]
    check(rm[0]["verified"] is True, "main breakpoint %r" % rm)
    c.request("configurationDone")

    b, i = stop(c, 0)
    check(b["reason"] == "breakpoint", "stop %r" % b)
    f = c.top_frame(1)
    check(f[0]["name"] == "Rect>>area" and f[0]["line"] == 7, "frame %r" % f[0])
    check(f[0]["source"]["path"].lower() == FAKE + "\\_geom.st", "geom path %r" % f[0]["source"])
    check(f[1]["source"]["path"].lower() == FAKE + "\\main.st", "main path %r" % f[1]["source"])
    changed = [e["body"]["breakpoint"] for e in c.events if e.get("event") == "breakpoint"]
    check(any(bp["id"] == rg[0]["id"] and bp["verified"] for bp in changed),
          "breakpoint verified on load %r" % changed)
    loaded = [e["body"]["source"] for e in c.events if e.get("event") == "loadedSource"]
    check(any(s.get("path", "").lower() == FAKE + "\\_geom.st" for s in loaded), "loadedSource %r" % loaded)

    c.request("continue", {"threadId": 1})
    b, i = stop(c, i)
    f = c.top_frame(1)
    check(f[0]["line"] == 6, "main line 6 %r" % f[0])
    c.request("stepIn", {"threadId": 1})
    b, i = stop(c, i)
    f = c.top_frame(1)
    src = f[0]["source"]
    check(f[0]["name"] == "Integer>>to:" and "path" not in src and src.get("sourceReference", 0) > 0
          and src.get("presentationHint") == "deemphasize", "kernel frame %r" % f[0])
    origin = src["origin"]
    check(origin.replace("\\", "/").endswith("kernel/collection.st"), "origin %r" % origin)
    body = c.request("source", {"sourceReference": src["sourceReference"],
                                "source": src})["body"]
    with open(origin, "rb") as fh:
        want = fh.read().decode("utf-8")
    check(body["content"] == want, "source content")
    line = f[0]["line"]
    rk = c.request("setBreakpoints", {"source": src, "breakpoints": [{"line": line}]})["body"]["breakpoints"]
    check(rk[0]["verified"] and rk[0]["source"].get("sourceReference") == src["sourceReference"],
          "breakpoint on the reference %r" % rk)
    c.request("continue", {"threadId": 1})
    b, i = stop(c, i)
    f = c.top_frame(1)
    check(b["reason"] == "breakpoint" and f[0]["name"] == "Integer>>to:" and f[0]["line"] == line
          and f[1]["line"] == 7, "stop on the reference %r" % f[:2])

    ls = c.request("loadedSources")["body"]["sources"]
    paths = [s.get("path", "").lower() for s in ls]
    check(FAKE + "\\main.st" in paths and FAKE + "\\_geom.st" in paths, "loaded %r" % paths)
    check(any(s.get("sourceReference", 0) > 0 and s["name"] == "collection.st" for s in ls),
          "kernel in loadedSources %r" % ls)
    c.request("continue", {"threadId": 1})
    rc = c.finish()
    check(c.output_text().replace("\r\n", "\n") == "24\n3\n5\ndone\n", "output %r" % c.output_text())
    check(rc == 0, "exit")
    print("OK", case)
finally:
    c.kill()
PYEOF

for case in mapped unmapped invalid; do
    "${PYTHON:-python3}" "$WORK/driver.py" "$HERE" "$PROTOST" "$WORK/proj" "$case"
done
echo OK
