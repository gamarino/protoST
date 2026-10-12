#!/usr/bin/env python3
"""perf stat -r N -e cycles,instructions for builds given as LABEL=PATH or
LABEL=PATH@LIBDIR (the same binary run with LD_LIBRARY_PATH=LIBDIR), on the
workloads of benchmarks/compare_builds.py. Before each perf call the script
checks with ldd that the binary resolves libprotoCore inside LIBDIR. Counts
include every thread of the process, so the collector's CPU is included.
Builds alternate their order from workload to workload. A run whose output
has no VERIFIED line is recorded as an error and makes the script exit 1.

Usage: perfstat.py --json OUT.json [--repeat 5] LABEL=PATH[@LIBDIR] ...
Run from the repository root."""
import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from compare_builds import workloads, competitors_running, resolved_core  # noqa: E402
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
    if out.stdout.count("VERIFIED") != repeat:
        res["error"] = f"{out.stdout.count('VERIFIED')} VERIFIED lines for {repeat} runs"
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("builds", nargs="+")
    ap.add_argument("--repeat", type=int, default=5)
    ap.add_argument("--json", required=True)
    ap.add_argument("--only", default=None)
    a = ap.parse_args()
    builds = []
    for spec in a.builds:
        label, rest = spec.split("=", 1)
        path, _, libdir = rest.partition("@")
        builds.append((label, path, {"LD_LIBRARY_PATH": libdir} if libdir else {}))
    wl = workloads()
    if a.only:
        wl = [w for w in wl if w[0] in set(a.only.split(","))]
    rows, errors = [], []
    for i, (name, tail, env, _expected) in enumerate(wl):
        order = builds[i % len(builds):] + builds[:i % len(builds)]
        row = {"name": name, "env": env, "load": os.getloadavg()[0],
               "contenders": competitors_running(), "results": {}}
        for label, path, lenv in order:
            core = resolved_core(path, {**os.environ, **lenv})
            if lenv and lenv["LD_LIBRARY_PATH"] not in core:
                sys.exit(f"{label}: ldd resolves {core!r}")
            r = perf(path, tail, {**env, **lenv}, a.repeat)
            r["protoCore"] = core
            row["results"][label] = r
            if "error" in r:
                errors.append(f"{name} [{label}]: {r['error']}")
        rows.append(row)
        cells = "  ".join(f"{l}: {(r.get('cycles') or 0) / 1e6:.0f} Mcyc {(r.get('instructions') or 0) / 1e6:.0f} Minstr"
                          for l, r in row["results"].items())
        print(f"{name:<24} {cells}  load {row['load']:.1f}", flush=True)
    Path(a.json).write_text(json.dumps({"repeat": a.repeat, "builds": builds, "rows": rows,
                                        "errors": errors}, indent=1))
    if errors:
        print("ERRORS:", *errors, sep="\n  ")
        sys.exit(1)


if __name__ == "__main__":
    main()
