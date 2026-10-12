#!/usr/bin/env python3
"""Tables for the report from results/compare.json, perfstat.json, demo2*.json
and the CPython runs. Usage: tab.py RESULTS_DIR"""
import json
import math
import statistics as st
import sys
from pathlib import Path

R = Path(sys.argv[1])
med = st.median


def pct(a, b):
    return (b - a) / a * 100.0


d = json.load(open(R / "compare.json"))
print("## wall / CPU / RSS (medians of 7 per copy; pooled 14 per version)\n")
print("| Workload | 2.14.5 wall | 2.14.6 wall | Δ wall (pooled) | A/A 2.14.5 | A/A 2.14.6 | 2.14.5 CPU | 2.14.6 CPU | Δ CPU | 2.14.5 RSS MB | 2.14.6 RSS MB |")
print("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
rows = {}
for w in d["workloads"]:
    s = w["samples"]
    p5 = s["2145a"] + s["2145b"]
    p6 = s["2146a"] + s["2146b"]
    w5, w6 = med([x["ms"] for x in p5]), med([x["ms"] for x in p6])
    c5 = med([x["user_ms"] + x["sys_ms"] for x in p5])
    c6 = med([x["user_ms"] + x["sys_ms"] for x in p6])
    r5 = med([x["rss_kb"] for x in p5]) / 1024
    r6 = med([x["rss_kb"] for x in p6]) / 1024
    m = w["median_ms"]
    aa5 = pct(m["2145a"], m["2145b"])
    aa6 = pct(m["2146a"], m["2146b"])
    rows[w["name"]] = (w5, w6)
    print(f"| {w['name']} | {w5:,.0f} | {w6:,.0f} | {pct(w5, w6):+.1f} % | {aa5:+.1f} % | {aa6:+.1f} % | "
          f"{c5:,.0f} | {c6:,.0f} | {pct(c5, c6):+.1f} % | {r5:.0f} | {r6:.0f} |")

print("\n## scaling (pooled medians)\n")
for v, i in (("2.14.5", 0), ("2.14.6", 1)):
    pool, one = rows["parallel_speedup pool"][i], rows["parallel_speedup w=1"][i]
    s1 = rows["saturation_big w=1"][i]
    sc = ", ".join(f"w={k}: {s1 / rows[f'saturation_big w={k}'][i]:.2f}x" for k in range(2, 7))
    mt = rows["message_throughput"][i]
    print(f"- {v}: parallel_speedup pool vs 1 worker {one / pool:.2f}x; saturation_big vs w=1: {sc}; "
          f"message_throughput {mt:.0f} ms = {2000 / (mt / 1000):,.0f} round trips/s incl. start-up")

p = json.load(open(R / "perfstat.json"))
print("\n## perf stat -r 5 (all threads)\n")
print("| Workload | 2.14.5 Mcycles | 2.14.6 Mcycles | Δ cycles | 2.14.5 Minstr | 2.14.6 Minstr | Δ instr |")
print("|---|---:|---:|---:|---:|---:|---:|")
for r in p["rows"]:
    a, b = r["results"]["2.14.5"], r["results"]["2.14.6"]
    print(f"| {r['name']} | {a['cycles'] / 1e6:,.0f} | {b['cycles'] / 1e6:,.0f} | {pct(a['cycles'], b['cycles']):+.1f} % | "
          f"{a['instructions'] / 1e6:,.0f} | {b['instructions'] / 1e6:,.0f} | {pct(a['instructions'], b['instructions']):+.1f} % |")

for f in sorted(R.glob("demo2*.json")):
    dd = json.load(open(f))
    print(f"\n## {f.name}\n")
    for side in ("2.14.5", "2.14.6"):
        rr = [x for x in dd["records"] if x["side"] == side]
        if not rr:
            continue
        one = [x["one_ms"] for x in rr]; tw = [x["twelve_ms"] for x in rr]; ra = [x["ratio"] for x in rr]
        ld = [x["load"] for x in rr]
        print(f"- {side} (n={len(rr)}): one thread median {med(one):.0f} ms ({min(one)}-{max(one)}), "
              f"12 actors {med(tw):.0f} ms ({min(tw)}-{max(tw)}), ratio median {med(ra):.2f} ({min(ra)}-{max(ra)}), "
              f"load {min(ld)}-{max(ld)}")
