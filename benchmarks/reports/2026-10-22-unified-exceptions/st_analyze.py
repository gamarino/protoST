#!/usr/bin/env python3
"""protoST A/B analysis: medians of the three per-round harness medians,
AFTER/BEFORE per cell, geomeans, and the probe table."""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from uex_common import med, gmean, probe_table
OUT = sys.argv[1]
def parse(path):
    d = {}
    txt = open(path).read()
    m = re.search(r"startup: protoST ([\d.]+) ms, CPython ([\d.]+) ms", txt)
    d["startup"] = (float(m.group(1)), float(m.group(2)))
    for m in re.finditer(r"^  (\w+)\s+protoST\s+([\d.]+|FAIL) ms\s+CPython\s+([\d.]+|FAIL) ms", txt, re.M):
        f = lambda s: None if s == "FAIL" else float(s)
        d[m.group(1)] = (f(m.group(2)), f(m.group(3)))
    m = re.search(r"parallel_speedup\s+pool ([\d.]+) ms, 1 worker ([\d.]+) ms", txt)
    d["parallel_speedup (pool)"] = (float(m.group(1)), None); d["parallel_speedup (1 worker)"] = (float(m.group(2)), None)
    m = re.search(r"cooperative_yield\s+([\d.]+) ms\s+message_throughput ([\d.]+) ms", txt)
    d["cooperative_yield"] = (float(m.group(1)), None); d["message_throughput"] = (float(m.group(2)), None)
    for w, t in re.findall(r"w=(\d+): ([\d.]+)", txt):
        d["saturation_big w=%s" % w] = (float(t), None)
    d["_verified"] = "All runs verified." in txt
    return d
R = {(v, r): parse(os.path.join(OUT, "st_%s_r%d.log" % (v, r))) for v in ("before", "after") for r in (1, 2, 3)}
print("All 6 harness runs verified:", all(x["_verified"] for x in R.values()))
keys = [k for k in R[("after", 1)] if not k.startswith("_")]
print("| Workload | CPython | BEFORE | AFTER | A/B | BEFORE rounds | AFTER rounds |")
print("|---|---:|---:|---:|---:|---|---|")
ratios, comp, act = {}, [], []
for k in keys:
    b = [R[("before", r)][k][0] for r in (1, 2, 3)]; a = [R[("after", r)][k][0] for r in (1, 2, 3)]
    py = med([R[(v, r)][k][1] for v in ("before", "after") for r in (1, 2, 3)])
    ratio = med(a) / med(b)
    ratios[k] = ratio
    (act if ("w=" in k or k in ("parallel_speedup (pool)", "parallel_speedup (1 worker)", "cooperative_yield", "message_throughput")) else comp).append(ratio) if k != "startup" else None
    print("| `%s` | %s | %.1f | %.1f | %.3f | %s | %s |" % (k, "%.1f" % py if py else "—", med(b), med(a), ratio,
          "/".join("%.0f" % x for x in b), "/".join("%.0f" % x for x in a)))
print()
print("geomean A/B comparables (7, startup excluded): %.3f" % gmean(comp))
print("geomean A/B actor cells (%d): %.3f" % (len(act), gmean(act)))
print("geomean A/B all (%d): %.3f" % (len(comp) + len(act), gmean(comp + act)))
for v in ("before", "after"):
    rs = [med([R[(v, r)][k][0] for r in (1, 2, 3)]) / med([R[(vv, r)][k][1] for vv in ("before", "after") for r in (1, 2, 3)])
          for k in keys if k != "startup" and R[(v, 1)][k][1]]
    print("geomean %s / CPython 3.14 (7 comparables, whole process): %.2fx" % (v, gmean(rs)))
print()
t, raw, su = probe_table(os.path.join(OUT, "probes.txt"), ["before", "after"], [("before", "after")])
print(t); print(su); print(); print("\n".join(raw))
