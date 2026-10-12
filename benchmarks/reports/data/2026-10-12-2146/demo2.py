#!/usr/bin/env python3
"""Runs the talk's demo 2 (docs/talks/2026-10-15-fas/demos/02-actors-parallelism.st)
RUNS times per side, alternating sides run by run, with the same protost binary
and LD_LIBRARY_PATH selecting the protoCore library (checked with ldd before
every run). Each run must exit 0 and print the expected non-timing lines
(4946 primes twice, true); the two times and the printed ratio are recorded
with the 1-minute load and any competing process.

Usage: demo2.py --json OUT.json [--runs 12] LABEL=LIBDIR ...
Run from the repository root; PROTOST names the binary (default /usr/bin/protost)."""
import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from compare_builds import competitors_running, resolved_core  # noqa: E402

ROOT = Path(__file__).resolve().parents[4]
DEMO = ROOT / "docs/talks/2026-10-15-fas/demos/02-actors-parallelism.st"
EXPECTED = ROOT / "docs/talks/2026-10-15-fas/demos/expected/02-actors-parallelism.out"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sides", nargs="+")
    ap.add_argument("--runs", type=int, default=12)
    ap.add_argument("--json", required=True)
    a = ap.parse_args()
    binary = os.environ.get("PROTOST", "/usr/bin/protost")
    sides = [s.split("=", 1) for s in a.sides]
    want = [l for l in EXPECTED.read_text().splitlines() if not l.startswith("time")]
    rec, failures = [], []
    for rnd in range(a.runs):
        k = rnd % len(sides)
        for label, libdir in sides[k:] + sides[:k]:
            env = {**os.environ, "LD_LIBRARY_PATH": libdir}
            core = resolved_core(binary, env)
            if libdir not in core:
                sys.exit(f"{label}: ldd resolves {core!r}")
            load, cont = os.getloadavg()[0], competitors_running()
            p = subprocess.run(["timeout", "60", binary, str(DEMO)], env=env,
                               capture_output=True, text=True)
            lines = p.stdout.splitlines()
            got = [l for l in lines if not l.startswith("time")]
            t = {m.group(1): m.group(2) for m in (re.match(r"time (.*): ([0-9.]+)", l) for l in lines) if m}
            ok = p.returncode == 0 and got == want and len(t) == 3
            r = {"side": label, "round": rnd + 1, "ok": ok, "load": round(load, 2), "contenders": cont,
                 "protoCore": core, "one_ms": int(t.get("one thread", 0)),
                 "twelve_ms": int(t.get("12 actors", 0)), "ratio": float(t.get("speedup", 0))}
            rec.append(r)
            if not ok:
                failures.append(f"{label} round {rnd + 1}: rc {p.returncode}, {p.stdout!r} {p.stderr[:200]!r}")
            print(f"{label:>8} one {r['one_ms']:5d} ms  12 actors {r['twelve_ms']:4d} ms  "
                  f"ratio {r['ratio']:.1f}  load {load:.1f} {' '.join(cont)}", flush=True)
    Path(a.json).write_text(json.dumps({"binary": binary, "runs": a.runs, "records": rec,
                                        "failures": failures}, indent=1))
    if failures:
        print("FAILURES:", *failures, sep="\n  ")
        sys.exit(1)


if __name__ == "__main__":
    main()
