#!/usr/bin/env python3
"""
Performance benchmark harness for protoST.

Every benchmark verifies its own work: a protoST benchmark ends by printing
`VERIFIED <value>` as its last line (and signals an Error if its result is
wrong), and each CPython twin prints `BENCH_RESULT ... result=<value>`. The
harness compares every run's value with the expected one and exits non-zero
if any run is missing it or reports another value, so a crash, a hang or a
wrong answer can never be reported as a time.

Two benchmark families are measured:

* **Comparable workloads** -- the protoPython core benchmark suite translated
  to protoST. Each `.st` file in `benchmarks/comparable/` computes the same
  result with the same N as its protoPython `.py` twin; the harness runs the
  twin with CPython (`BENCH_N` set to the same N) and, when a built `protopy`
  is found, with protoPython.

* **Actor-model benchmarks** -- protoST-specific workloads in
  `benchmarks/actors/`: parallel speedup, cooperative-yield scaling, message
  throughput and the worker-scaling curve of `saturation_big.st`.

Each measurement is WARMUP warmup runs (discarded) plus RUNS timed runs; the
report gives the median and the min-max spread of wall-clock time, which for
these small N includes process start-up (measured separately and reported).

Usage:
  python3 benchmarks/run_benchmarks.py [--output PATH] [--runs N] [--warmup N]
                                       [--no-python] [--skip-actors]

Environment:
  PROTOST_BIN   protost binary        (default: build_release/protost)
  PROTOPY_BIN   protopy binary        (default: autodetected; skipped if absent)
  CPYTHON_BIN   python interpreter    (default: python3)
"""

import argparse
import math
import os
import platform
import shutil
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent
REPORTS_DIR = SCRIPT_DIR / "reports"
COMPARABLE_DIR = SCRIPT_DIR / "comparable"
ACTORS_DIR = SCRIPT_DIR / "actors"
PROTOPYTHON_BENCH = PROJECT_ROOT.parent / "protoPython" / "benchmarks"

TIMEOUT = 300  # seconds per run

# name, protoST file, CPython twin, N passed to the twin, expected result.
COMPARABLE = [
    ("int_sum_loop",      "int_sum_loop.st",      "int_sum_loop.py",       100000, 4999950000),
    ("fib",               "fib.st",               "call_recursion.py",         25,      75025),
    ("list_append",       "list_append.st",       "list_append_loop.py",    10000,      10000),
    ("str_concat",        "str_concat.st",        "str_concat_loop.py",      2000,       2000),
    ("attr_lookup",       "attr_lookup.st",       "attr_lookup.py",        100000,     600000),
    ("range_iterate",     "range_iterate.st",     "range_iterate.py",      100000,     100000),
    ("exception_latency", "exception_latency.st", "exception_latency.py",   50000,      50000),
]

ACTOR_EXPECTED = {
    "parallel_speedup": 135000900000,
    "cooperative_yield": 1000000,
    "message_throughput": 2000,
    "saturation_big": 20004000000,
}

FAILURES = []


def median(xs):
    s = sorted(xs)
    n = len(s)
    if n == 0:
        return 0.0
    return s[n // 2] if n % 2 else (s[n // 2 - 1] + s[n // 2]) / 2


def geomean(xs):
    xs = [x for x in xs if x and x > 0]
    if not xs:
        return 0.0
    return math.exp(sum(math.log(x) for x in xs) / len(xs))


def run_cmd(cmd, env=None, timeout=TIMEOUT):
    """Run once; return (elapsed_ms, returncode, timed_out, stdout, stderr)."""
    full_env = {**os.environ, **(env or {})}
    start = time.perf_counter()
    try:
        p = subprocess.run(cmd, cwd=PROJECT_ROOT, env=full_env, timeout=timeout,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                           stdin=subprocess.DEVNULL)
        return (time.perf_counter() - start) * 1000.0, p.returncode, False, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return (time.perf_counter() - start) * 1000.0, -1, True, "", ""


def verified_value(stdout):
    """The value of a protoST run's final `VERIFIED <value>` line, or None."""
    lines = [l.strip() for l in stdout.strip().splitlines() if l.strip()]
    if not lines or not lines[-1].startswith("VERIFIED "):
        return None
    return lines[-1][len("VERIFIED "):]


def bench_result_value(stdout):
    """The `result=` field of a Python twin's BENCH_RESULT line, or None."""
    for line in stdout.splitlines():
        if line.startswith("BENCH_RESULT"):
            for field in line.split():
                if field.startswith("result="):
                    return field[len("result="):]
    return None


def timed(label, cmd, expected, extract, runs, warmup, env=None):
    """Warmup + timed runs, each checked against `expected` with `extract`.
    Returns (median_ms, min_ms, max_ms) or None when any run fails."""
    samples = []
    for i in range(warmup + runs):
        ms, rc, to, out, err = run_cmd(cmd, env)
        got = extract(out)
        if to or rc != 0 or got != str(expected):
            why = "timed out" if to else (f"exit {rc}" if rc != 0 else
                                          f"reported {got!r}, expected {expected}")
            FAILURES.append(f"{label}: run {i + 1} {why}"
                            + (f"; stderr: {err.strip()[:200]}" if err.strip() else ""))
            return None
        if i >= warmup:
            samples.append(ms)
    return median(samples), min(samples), max(samples)


def fmt(t):
    return "FAIL" if t is None else f"{t[0]:.1f}"


def spread(t):
    return "" if t is None else f"{t[1]:.1f}–{t[2]:.1f}"


def find_protopy():
    if os.environ.get("PROTOPY_BIN"):
        p = Path(os.environ["PROTOPY_BIN"])
        return p if p.exists() else None
    root = PROJECT_ROOT.parent / "protoPython"
    for sub in ("build_release", "build"):
        cand = root / sub / "protopy"
        if cand.exists() and os.access(cand, os.X_OK):
            return cand
    return None


def twin_cmd(interpreter, py_path, n):
    """exception_latency.py prints nothing and takes N on argv: call its
    run_bench(n) and print the result in the BENCH_RESULT form."""
    if py_path.name == "exception_latency.py":
        code = (f"exec(open({str(py_path)!r}).read()); "
                f"print('BENCH_RESULT name=exception_latency N={n} result=%d' % run_bench({n}))")
        return [interpreter, "-c", code]
    return [interpreter, str(py_path)]


def host_facts(protost, cpython):
    cpu = platform.machine()
    try:
        for line in Path("/proc/cpuinfo").read_text().splitlines():
            if line.startswith("model name"):
                cpu = line.split(":", 1)[1].strip()
                break
    except OSError:
        pass
    try:
        git = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=PROJECT_ROOT,
                             capture_output=True, text=True).stdout.strip()
    except OSError:
        git = "?"
    version = subprocess.run([str(protost), "--version"], capture_output=True,
                             text=True).stdout.strip()
    pyver = subprocess.run([cpython, "--version"], capture_output=True,
                           text=True).stdout.strip()
    return cpu, git, version, pyver


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--output", default=None)
    ap.add_argument("--runs", type=int, default=5)
    ap.add_argument("--warmup", type=int, default=2)
    ap.add_argument("--no-python", action="store_true", help="skip the CPython/protopy twins")
    ap.add_argument("--skip-actors", action="store_true")
    args = ap.parse_args()

    protost = Path(os.environ.get("PROTOST_BIN", PROJECT_ROOT / "build_release" / "protost"))
    if not protost.exists():
        sys.exit(f"protost binary not found: {protost} -- build it first.")
    cpython = os.environ.get("CPYTHON_BIN", "python3")
    if not args.no_python and not shutil.which(cpython):
        sys.exit(f"CPython interpreter not found: {cpython}")
    protopy = None if args.no_python else find_protopy()

    cpu, git, version, pyver = host_facts(protost, cpython)
    load_before = os.getloadavg()
    ncpu = os.cpu_count() or 1
    print(f"protoST benchmarks -- {datetime.now():%Y-%m-%d %H:%M}")
    print(f"  host {cpu} ({ncpu} logical CPUs), load {load_before[0]:.2f}")
    print(f"  {version} @ {git}; {pyver}; protopy {protopy or 'not found'}")
    print(f"  {args.warmup} warmup + {args.runs} timed runs, median reported\n")
    if load_before[0] > 1.0:
        print("  WARNING: the machine is not idle (1-minute load > 1.0)\n")

    startup = timed("startup", [str(protost), "-e", "'VERIFIED ' , 42 printString"],
                    42, verified_value, args.runs, args.warmup)
    py_startup = None if args.no_python else timed(
        "cpython startup", [cpython, "-c", "print('BENCH_RESULT result=42')"],
        42, bench_result_value, args.runs, args.warmup)
    print(f"  startup: protoST {fmt(startup)} ms"
          + ("" if args.no_python else f", CPython {fmt(py_startup)} ms"))

    rows = []
    for name, st_file, py_file, n, expected in COMPARABLE:
        st = timed(name, [str(protost), str(COMPARABLE_DIR / st_file)], expected,
                   verified_value, args.runs, args.warmup)
        py = pp = None
        py_path = PROTOPYTHON_BENCH / py_file
        if not args.no_python and py_path.exists():
            py = timed(f"{name} (CPython)", twin_cmd(cpython, py_path, n), expected,
                       bench_result_value, args.runs, args.warmup, env={"BENCH_N": str(n)})
            if protopy:
                pp = timed(f"{name} (protopy)", twin_cmd(str(protopy), py_path, n), expected,
                           bench_result_value, args.runs, args.warmup, env={"BENCH_N": str(n)})
        print(f"  {name:<18} protoST {fmt(st):>8} ms   CPython {fmt(py):>8} ms"
              + (f"   protopy {fmt(pp):>8} ms" if protopy else ""))
        rows.append((name, n, st, py, pp))

    actors = {}
    if not args.skip_actors:
        par = ACTORS_DIR / "parallel_speedup.st"
        actors["parallel_pool"] = timed("parallel_speedup", [str(protost), str(par)],
                                        ACTOR_EXPECTED["parallel_speedup"], verified_value,
                                        args.runs, args.warmup)
        actors["parallel_one"] = timed("parallel_speedup w=1", [str(protost), str(par)],
                                       ACTOR_EXPECTED["parallel_speedup"], verified_value,
                                       args.runs, args.warmup, env={"PROTOST_WORKERS": "1"})
        actors["coop"] = timed("cooperative_yield", [str(protost), str(ACTORS_DIR / "cooperative_yield.st")],
                               ACTOR_EXPECTED["cooperative_yield"], verified_value,
                               args.runs, args.warmup, env={"PROTOST_WORKERS": "2"})
        actors["throughput"] = timed("message_throughput", [str(protost), str(ACTORS_DIR / "message_throughput.st")],
                                     ACTOR_EXPECTED["message_throughput"], verified_value,
                                     args.runs, args.warmup)
        scaling = []
        for w in range(1, min(6, ncpu) + 1):
            scaling.append((w, timed(f"saturation_big w={w}",
                                     [str(protost), str(ACTORS_DIR / "saturation_big.st")],
                                     ACTOR_EXPECTED["saturation_big"], verified_value,
                                     args.runs, args.warmup, env={"PROTOST_WORKERS": str(w)})))
        actors["scaling"] = scaling
        print(f"  parallel_speedup   pool {fmt(actors['parallel_pool'])} ms, 1 worker {fmt(actors['parallel_one'])} ms")
        print(f"  cooperative_yield  {fmt(actors['coop'])} ms   message_throughput {fmt(actors['throughput'])} ms")
        print("  saturation_big     " + ", ".join(f"w={w}: {fmt(t)}" for w, t in scaling))

    load_after = os.getloadavg()
    if args.output:
        write_report(Path(args.output), cpu, ncpu, git, version, pyver, protopy,
                     load_before, load_after, args, startup, py_startup, rows, actors)
        print(f"\nReport written: {args.output}")

    if FAILURES:
        print("\nVERIFICATION FAILURES:")
        for f in FAILURES:
            print(f"  {f}")
        sys.exit(1)
    print("\nAll runs verified.")


def write_report(path, cpu, ncpu, git, version, pyver, protopy, load_before, load_after,
                 args, startup, py_startup, rows, actors):
    L = []
    L.append(f"# protoST benchmarks — {datetime.now():%Y-%m-%d}")
    L.append("")
    L.append(f"- **Host:** {cpu}, {ncpu} logical CPUs, {platform.system()} {platform.release()}")
    L.append(f"- **Load average (1 min):** {load_before[0]:.2f} before, {load_after[0]:.2f} after")
    L.append(f"- **protoST:** {version} at commit `{git}`")
    L.append(f"- **CPython:** {pyver}" + (f"; **protoPython:** `{protopy}`" if protopy else ""))
    L.append(f"- **Method:** {args.warmup} warmup + {args.runs} timed runs per measurement; "
             "median wall-clock, min–max in parentheses. Every run verified its result "
             "(`VERIFIED` / `BENCH_RESULT`); a failed run fails the harness.")
    L.append("")
    L.append("## Start-up")
    L.append("")
    L.append(f"A process that evaluates one expression: protoST {fmt(startup)} ms ({spread(startup)})"
             + ("" if py_startup is None else f", CPython {fmt(py_startup)} ms ({spread(py_startup)})")
             + ". The comparable workloads below use small N, so start-up is part of every time.")
    L.append("")
    L.append("## Comparable workloads")
    L.append("")
    L.append("Same algorithm, same N and same verified result as the protoPython twin. "
             "`Ratio` is protoST ÷ CPython (>1 means protoST is slower).")
    L.append("")
    L.append("| Benchmark | N | protoST ms | CPython ms | Ratio |")
    L.append("|---|---:|---:|---:|---:|")
    ratios = []
    for name, n, st, py, _pp in rows:
        ratio = (st[0] / py[0]) if (st and py) else None
        if ratio:
            ratios.append(ratio)
        L.append(f"| {name} | {n} | {fmt(st)} ({spread(st)}) | {fmt(py)} ({spread(py)}) | "
                 f"{f'{ratio:.2f}×' if ratio else '—'} |")
    L.append(f"| **Geomean** | | | | **{geomean(ratios):.2f}×** |")
    L.append("")
    if actors:
        L.append("## Actors")
        L.append("")
        pp, p1 = actors.get("parallel_pool"), actors.get("parallel_one")
        if pp and p1:
            L.append(f"- **Parallel speedup** (12 CPU-bound actors): {fmt(p1)} ms with one worker, "
                     f"{fmt(pp)} ms with the default pool: **{p1[0] / pp[0]:.2f}×**.")
        if actors.get("coop"):
            L.append(f"- **Cooperative yield:** 1000 actors, each parked on a `wait`, on 2 worker "
                     f"threads: {fmt(actors['coop'])} ms.")
        if actors.get("throughput"):
            t = actors["throughput"][0]
            L.append(f"- **Message throughput:** 2000 round-trip sends (send + `wait`): {t:.1f} ms, "
                     f"about {2000 / (t / 1000):,.0f} round trips per second including start-up.")
        if actors.get("scaling"):
            L.append("")
            L.append("Worker scaling of `saturation_big.st` (32 actors × 50 messages of CPU work):")
            L.append("")
            L.append("| Workers | ms | Speedup vs 1 |")
            L.append("|---:|---:|---:|")
            base = actors["scaling"][0][1]
            for w, t in actors["scaling"]:
                sp = f"{base[0] / t[0]:.2f}×" if (base and t) else "—"
                L.append(f"| {w} | {fmt(t)} ({spread(t)}) | {sp} |")
        L.append("")
    if FAILURES:
        L.append("## Verification failures")
        L.append("")
        L.extend(f"- {f}" for f in FAILURES)
        L.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(L) + "\n")


if __name__ == "__main__":
    main()
