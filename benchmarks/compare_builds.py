#!/usr/bin/env python3
"""
Compare several protoST builds on the benchmark suite, interleaved.

Each build is given as LABEL=PATH (a protost binary). For every workload the
script runs WARMUP rounds (discarded) and then RUNS rounds; within a round it
runs every build once, rotating the order from round to round, so a change in
machine load affects all builds alike. Every run is verified exactly as in
run_benchmarks.py: the program's last line must be `VERIFIED <expected>`, the
exit status must be 0, and any failure makes the script exit non-zero.

For each run it records the wall-clock time, the peak resident set size of
the child (wait4), its user and system CPU time (wait4), the 1-minute load average and whether a process from a
list of known competitors (compilers, build tools, test runners, protoCore
runtimes) was
running when the run started.

A build can also be one binary run against another protoCore library:
`--libdir LABEL=DIR` sets LD_LIBRARY_PATH=DIR for that build's runs. Before
every run of such a build the script runs `ldd` with the same environment and
records the libprotoCore it resolves; a run that would load a library outside
DIR is a failure, so a mislinked run can never be reported as a time.

Usage:
  python3 benchmarks/compare_builds.py --json OUT.json \
      old=/usr/bin/protost new=build_release/protost [--runs 3] [--warmup 1]
  python3 benchmarks/compare_builds.py --json OUT.json \
      a=/usr/bin/protost b=/usr/bin/protost --libdir b=/opt/protoCore-next/lib
"""

import argparse
import json
import os
import platform
import re
import selectors
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

from run_benchmarks import (ACTOR_EXPECTED, ACTORS_DIR, COMPARABLE, COMPARABLE_DIR,
                            PROJECT_ROOT, TIMEOUT, median, verified_value)

# Compilers, build tools, test runners and every protoCore runtime binary.
COMPETITORS_RE = re.compile(r"^(cc1plus|cc1|ctest|test262|node|lto1|ld|make|gmake|ninja)$|^proto")


def competitors_running():
    try:
        out = subprocess.run(["ps", "-eo", "comm="], capture_output=True, text=True).stdout
    except OSError:
        return []
    names = {line.strip() for line in out.splitlines()}
    return sorted(n for n in names if COMPETITORS_RE.search(n))


def workloads():
    """(name, argv tail, env, expected)."""
    w = [("startup", ["-e", "'VERIFIED ' , 42 printString"], {}, 42)]
    for name, st_file, _py, _n, expected in COMPARABLE:
        w.append((name, [str(COMPARABLE_DIR / st_file)], {}, expected))
    par = str(ACTORS_DIR / "parallel_speedup.st")
    w.append(("parallel_speedup pool", [par], {}, ACTOR_EXPECTED["parallel_speedup"]))
    w.append(("parallel_speedup w=1", [par], {"PROTOST_WORKERS": "1"},
              ACTOR_EXPECTED["parallel_speedup"]))
    w.append(("cooperative_yield w=2", [str(ACTORS_DIR / "cooperative_yield.st")],
              {"PROTOST_WORKERS": "2"}, ACTOR_EXPECTED["cooperative_yield"]))
    w.append(("message_throughput", [str(ACTORS_DIR / "message_throughput.st")], {},
              ACTOR_EXPECTED["message_throughput"]))
    for k in range(1, 7):
        w.append((f"saturation_big w={k}", [str(ACTORS_DIR / "saturation_big.st")],
                  {"PROTOST_WORKERS": str(k)}, ACTOR_EXPECTED["saturation_big"]))
    return w


def resolved_core(binary, env):
    """The libprotoCore line ldd prints for `binary` under `env`."""
    ldd = subprocess.run(["ldd", binary], capture_output=True, text=True, env=env).stdout
    line = next((l.strip() for l in ldd.splitlines() if "libprotoCore" in l), "?")
    return line.split(" (0x")[0]  # without the load address, which ASLR varies


def build_env(build):
    if not build.get("libdir"):
        return {}
    return {"LD_LIBRARY_PATH": build["libdir"]}


def run_once(binary, tail, env):
    """Run once; reap the child with wait4 to get its own peak RSS."""
    full_env = {**os.environ, **env}
    contenders = competitors_running()
    load = os.getloadavg()[0]
    out_r, out_w = os.pipe()
    err_r, err_w = os.pipe()
    start = time.perf_counter()
    p = subprocess.Popen([binary] + tail, cwd=PROJECT_ROOT, env=full_env,
                         stdout=out_w, stderr=err_w, stdin=subprocess.DEVNULL)
    os.close(out_w)
    os.close(err_w)
    chunks_out, chunks_err = [], []
    sel = selectors.DefaultSelector()
    sel.register(out_r, selectors.EVENT_READ, chunks_out)
    sel.register(err_r, selectors.EVENT_READ, chunks_err)
    open_fds = 2
    deadline = start + TIMEOUT
    while open_fds:
        remaining = deadline - time.perf_counter()
        if remaining <= 0:
            p.kill()
            break
        for key, _ in sel.select(timeout=remaining):
            data = os.read(key.fd, 65536)
            if not data:
                sel.unregister(key.fd)
                os.close(key.fd)
                open_fds -= 1
            else:
                key.data.append(data)
    _pid, status, ru = os.wait4(p.pid, 0)
    ms = (time.perf_counter() - start) * 1000.0
    p.returncode = os.waitstatus_to_exitcode(status)
    out = b"".join(chunks_out).decode(errors="replace")
    err = b"".join(chunks_err).decode(errors="replace")
    return {"ok": p.returncode == 0, "rc": p.returncode, "ms": ms, "rss_kb": ru.ru_maxrss,
            "user_ms": ru.ru_utime * 1000.0, "sys_ms": ru.ru_stime * 1000.0,
            "out": out, "err": err, "load": load, "contenders": contenders}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("builds", nargs="+", help="LABEL=PATH to a protost binary")
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--warmup", type=int, default=1)
    ap.add_argument("--json", required=True)
    ap.add_argument("--only", default=None, help="comma-separated workload names")
    ap.add_argument("--libdir", action="append", default=[],
                    help="LABEL=DIR: run that build with LD_LIBRARY_PATH=DIR (checked with ldd per run)")
    args = ap.parse_args()
    libdirs = dict(l.split("=", 1) for l in args.libdir)

    builds = []
    for b in args.builds:
        label, _, path = b.partition("=")
        if not path or not Path(path).exists():
            sys.exit(f"bad build spec or missing binary: {b}")
        libdir = libdirs.get(label)
        if libdir:
            libdir = str(Path(libdir).resolve())
        b_env = {**os.environ, **({"LD_LIBRARY_PATH": libdir} if libdir else {})}
        version = subprocess.run([path, "--version"], capture_output=True, text=True,
                                 env=b_env).stdout.strip()
        core = resolved_core(path, b_env)
        if libdir and libdir not in core:
            sys.exit(f"{label}: ldd resolves {core!r}, not a library in {libdir}")
        builds.append({"label": label, "path": path, "version": version, "protoCore": core,
                       "libdir": libdir})

    wl = workloads()
    if args.only:
        keep = set(args.only.split(","))
        wl = [w for w in wl if w[0] in keep]

    results = {"date": datetime.now().isoformat(timespec="seconds"),
               "host": platform.node(), "kernel": platform.release(),
               "runs": args.runs, "warmup": args.warmup, "builds": builds,
               "workloads": []}
    failures = []
    for name, tail, env, expected in wl:
        samples = {b["label"]: [] for b in builds}
        for rnd in range(args.warmup + args.runs):
            k = rnd % len(builds)
            order = builds[k:] + builds[:k]
            for b in order:
                run_env = {**env, **build_env(b)}
                core = resolved_core(b["path"], {**os.environ, **run_env})
                if core != b["protoCore"]:
                    failures.append(f"{name} [{b['label']}] round {rnd + 1}: ldd resolves "
                                    f"{core!r}, expected {b['protoCore']!r}")
                    continue
                r = run_once(b["path"], tail, run_env)
                got = verified_value(r.get("out", ""))
                if not r["ok"] or got != str(expected):
                    why = r.get("why") or (f"exit {r.get('rc')}" if not r["ok"]
                                           else f"reported {got!r}, expected {expected}")
                    failures.append(f"{name} [{b['label']}] round {rnd + 1}: {why}; "
                                    f"stderr: {r.get('err', '').strip()[:300]}")
                    continue
                if rnd >= args.warmup:
                    samples[b["label"]].append({"ms": round(r["ms"], 2), "rss_kb": r["rss_kb"],
                                                "user_ms": round(r["user_ms"], 2),
                                                "sys_ms": round(r["sys_ms"], 2),
                                                "load": round(r["load"], 2),
                                                "protoCore": core,
                                                "contenders": r["contenders"]})
        row = {"name": name, "env": env, "expected": expected, "samples": samples,
               "median_ms": {l: (median([s["ms"] for s in v]) if len(v) == args.runs else None)
                             for l, v in samples.items()},
               "median_rss_kb": {l: (median([s["rss_kb"] for s in v]) if len(v) == args.runs else None)
                                 for l, v in samples.items()},
               "median_cpu_ms": {l: (median([s["user_ms"] + s["sys_ms"] for s in v])
                                     if len(v) == args.runs else None)
                                 for l, v in samples.items()}}
        results["workloads"].append(row)
        cells = "  ".join(f"{l} {('%.1f' % m) if m else 'FAIL':>8} ms" for l, m in row["median_ms"].items())
        print(f"{name:<24} {cells}", flush=True)

    results["failures"] = failures
    Path(args.json).write_text(json.dumps(results, indent=1))
    if failures:
        print("\nVERIFICATION FAILURES:")
        for f in failures:
            print("  " + f)
        sys.exit(1)
    print("\nAll runs verified.")


if __name__ == "__main__":
    main()
