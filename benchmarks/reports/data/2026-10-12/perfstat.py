#!/usr/bin/env python3
"""perf stat -r N -e cycles,instructions for two or more protost builds on the
workloads of benchmarks/compare_builds.py. Counts include every thread of the
process (perf stat follows threads by default), so the collector's CPU is
included. Builds alternate their order from workload to workload.

Usage: perfstat.py --json OUT.json [--repeat 5] LABEL=PATH ...
Run from the repository root."""
import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from compare_builds import workloads, competitors_running  # noqa: E402
from run_benchmarks import PROJECT_ROOT  # noqa: E402


def perf(binary, tail, env, repeat):
    out = subprocess.run(["perf", "stat", "-r", str(repeat), "-x;", "-e", "cycles,instructions",
                          "--", binary] + tail, cwd=PROJECT_ROOT, env={**os.environ, **env, "LC_ALL": "C"},
                         capture_output=True, text=True)
    res = {}
    for line in out.stderr.splitlines():
        parts = line.split(";")
        if len(parts) > 3 and parts[2] in ("cycles", "instructions", "cpu_core/cycles/",
                                           "cpu_core/instructions/"):
            key = parts[2].split("/")[1] if "/" in parts[2] else parts[2]
            res[key] = float(parts[0]) if parts[0] not in ("<not counted>", "<not supported>") else None
            res[key + "_var_pct"] = parts[3].rstrip("%") if len(parts) > 3 else None
    if not any("VERIFIED" in l for l in out.stdout.splitlines()):
        res["error"] = "no VERIFIED line"
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("builds", nargs="+")
    ap.add_argument("--repeat", type=int, default=5)
    ap.add_argument("--json", required=True)
    a = ap.parse_args()
    builds = [b.split("=", 1) for b in a.builds]
    rows = []
    for i, (name, tail, env, _expected) in enumerate(workloads()):
        order = builds[i % len(builds):] + builds[:i % len(builds)]
        row = {"name": name, "env": env, "load": os.getloadavg()[0],
               "contenders": competitors_running(), "results": {}}
        for label, path in order:
            row["results"][label] = perf(path, tail, env, a.repeat)
        rows.append(row)
        cells = "  ".join(f"{l}: {r.get('cycles', 0) / 1e6:.0f} Mcyc {r.get('instructions', 0) / 1e6:.0f} Minstr"
                          for l, r in row["results"].items())
        print(f"{name:<24} {cells}", flush=True)
    Path(a.json).write_text(json.dumps({"repeat": a.repeat, "builds": builds, "rows": rows}, indent=1))


if __name__ == "__main__":
    main()
