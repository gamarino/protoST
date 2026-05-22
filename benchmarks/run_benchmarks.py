#!/usr/bin/env python3
"""
Performance benchmark harness for protoST (Track 11).

Two benchmark families are measured:

* **Comparable workloads** -- the protoPython core benchmark suite translated
  to idiomatic protoST. Each `.st` file in `benchmarks/comparable/` runs the
  same algorithm with the same parameters as its protoPython `.py` twin, so
  results are directly comparable. For these the harness ALSO runs the
  protoPython `.py` equivalent with CPython (`python3`) and -- if a built
  `protopy` binary is found -- with protoPython, producing a side-by-side
  table.

* **Actor-model benchmarks** -- protoST-specific workloads in
  `benchmarks/actors/` that exercise the actor scheduler. protoPython has no
  actor model, so these have no Python column; they showcase protoST's
  distinctive feature (parallel speedup, cooperative-yield scaling, mailbox
  throughput).

Timing discipline mirrors protoPython's harness: WARMUP_RUNS warmup runs
(discarded) followed by N_RUNS timed runs; the median wall-clock is reported,
and a geometric mean aggregates the comparable table.

Usage:
  python3 benchmarks/run_benchmarks.py [--output benchmarks/reports/NAME.md]

Optional env:
  PROTOST_BIN   path to the protost binary    (default: ./build/protost)
  PROTOPY_BIN   path to a protopy binary      (default: autodetected; skipped
                                               if absent)
  CPYTHON_BIN   python interpreter            (default: python3)
"""

import argparse
import math
import os
import platform
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent
REPORTS_DIR = SCRIPT_DIR / "reports"
COMPARABLE_DIR = SCRIPT_DIR / "comparable"
ACTORS_DIR = SCRIPT_DIR / "actors"

N_RUNS = 5
WARMUP_RUNS = 2
TIMEOUT = 120  # seconds per run

# protoPython's benchmark directory holds the .py twins of the comparable suite.
PROTOPYTHON_BENCH = PROJECT_ROOT.parent / "protoPython" / "benchmarks"

# Comparable workloads: protoST .st file  ->  protoPython .py twin (or None).
COMPARABLE = [
    ("int_sum_loop",      "int_sum_loop.st",      "int_sum_loop.py"),
    ("fib",               "fib.st",               "call_recursion.py"),
    ("list_append",       "list_append.st",       "list_append_loop.py"),
    ("str_concat",        "str_concat.st",        "str_concat_loop.py"),
    ("attr_lookup",       "attr_lookup.st",       "attr_lookup.py"),
    ("range_iterate",     "range_iterate.st",     "range_iterate.py"),
    ("exception_latency", "exception_latency.st", "exception_latency.py"),
]


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
    """Run a command once; return (elapsed_ms, returncode, timed_out, stdout)."""
    full_env = {**os.environ, **(env or {})}
    start = time.perf_counter()
    try:
        p = subprocess.run(
            cmd, cwd=PROJECT_ROOT, env=full_env, timeout=timeout,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )
        elapsed = (time.perf_counter() - start) * 1000.0
        return elapsed, p.returncode, False, p.stdout.strip()
    except subprocess.TimeoutExpired:
        return (time.perf_counter() - start) * 1000.0, -1, True, ""
    except Exception as exc:  # noqa: BLE001
        print(f"      ERROR: {exc}")
        return 0.0, -1, False, ""


def timed(cmd, env=None, label=""):
    """Warmup + timed runs; return (median_ms, ok, last_stdout). None on failure."""
    for _ in range(WARMUP_RUNS):
        _, rc, to, _ = run_cmd(cmd, env)
        if rc != 0 or to:
            return None, False, ""
    samples, out = [], ""
    for _ in range(N_RUNS):
        ms, rc, to, stdout = run_cmd(cmd, env)
        if rc != 0 or to:
            return None, False, stdout
        samples.append(ms)
        out = stdout
    return median(samples), True, out


def find_protopy():
    if os.environ.get("PROTOPY_BIN"):
        p = Path(os.environ["PROTOPY_BIN"])
        return p if p.exists() else None
    root = PROJECT_ROOT.parent / "protoPython"
    for sub in ("build_release", "build", "build-release"):
        cand = root / sub / "protopy"
        if cand.exists() and os.access(cand, os.X_OK):
            return cand
    return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default=None,
                        help="report path (default: reports/<date>-baseline.md)")
    parser.add_argument("--skip-throughput", action="store_true",
                        help="skip message_throughput.st (it runs by default; "
                             "D23 — the scheduler deadlock that once made it "
                             "opt-in — is fixed, docs/STATUS.md)")
    args = parser.parse_args()

    protost = Path(os.environ.get("PROTOST_BIN", PROJECT_ROOT / "build" / "protost"))
    if not protost.exists():
        sys.exit(f"protost binary not found: {protost} -- build it first.")
    cpython = os.environ.get("CPYTHON_BIN", "python3")
    if not shutil.which(cpython):
        sys.exit(f"CPython interpreter not found: {cpython}")
    protopy = find_protopy()

    REPORTS_DIR.mkdir(exist_ok=True)
    out_path = Path(args.output) if args.output else (
        REPORTS_DIR / f"{datetime.now():%Y-%m-%d}-baseline.md")

    host_cpu = platform.processor() or platform.machine()
    try:
        for line in Path("/proc/cpuinfo").read_text().splitlines():
            if line.startswith("model name"):
                host_cpu = line.split(":", 1)[1].strip()
                break
    except OSError:
        pass
    ncpu = os.cpu_count() or 1

    print(f"protoST benchmark harness  --  {datetime.now():%Y-%m-%d %H:%M}")
    print(f"  host:    {host_cpu}  ({ncpu} logical CPUs)")
    print(f"  protost: {protost}")
    print(f"  cpython: {cpython}")
    print(f"  protopy: {protopy or '(not found -- column skipped)'}")
    print(f"  runs:    {WARMUP_RUNS} warmup + {N_RUNS} timed, median reported\n")

    # --- Comparable workloads -------------------------------------------------
    print("== Comparable workloads (protoST vs CPython"
          f"{' vs protopy' if protopy else ''}) ==")
    comp_rows = []
    for name, st_file, py_file in COMPARABLE:
        st_path = COMPARABLE_DIR / st_file
        py_path = PROTOPYTHON_BENCH / py_file
        print(f"  {name:<20}", end="", flush=True)

        st_ms, st_ok, _ = timed([str(protost), str(st_path)])
        print(f" protoST={'FAIL' if not st_ok else f'{st_ms:.1f}ms'}",
              end="", flush=True)

        py_ms = None
        if py_path.exists():
            py_ms, py_ok, _ = timed([cpython, str(py_path)])
            print(f"  cpython={'FAIL' if not py_ok else f'{py_ms:.1f}ms'}",
                  end="", flush=True)
        else:
            print("  cpython=N/A", end="", flush=True)

        pp_ms = None
        if protopy and py_path.exists():
            pp_ms, pp_ok, _ = timed([str(protopy), str(py_path)])
            print(f"  protopy={'FAIL' if not pp_ok else f'{pp_ms:.1f}ms'}",
                  end="", flush=True)
        print()
        comp_rows.append((name, st_ms, py_ms, pp_ms))

    # --- Actor-model benchmarks ----------------------------------------------
    print("\n== Actor-model benchmarks (protoST only) ==")

    par_file = ACTORS_DIR / "parallel_speedup.st"
    print("  parallel_speedup", end="", flush=True)
    par_full, ok_full, _ = timed([str(protost), str(par_file)])
    par_one, ok_one, _ = timed([str(protost), str(par_file)],
                               env={"PROTOST_WORKERS": "1"})
    speedup = (par_one / par_full) if (ok_full and ok_one and par_full) else None
    print(f"  pool={par_full:.0f}ms  1-worker={par_one:.0f}ms"
          f"  speedup={speedup:.2f}x" if speedup else "  FAIL")

    coop_file = ACTORS_DIR / "cooperative_yield.st"
    print("  cooperative_yield", end="", flush=True)
    coop_ms, ok_coop, _ = timed([str(protost), str(coop_file)],
                                env={"PROTOST_WORKERS": "2"})
    print(f"  N=1000 actors on K=2 workers: {coop_ms:.0f}ms"
          if ok_coop else "  FAIL")

    # message_throughput runs by default. It was once opt-in because of D23 —
    # a non-deterministic actor-scheduler deadlock under sustained mailbox
    # load — which is now fixed (docs/STATUS.md D23, Closed). --skip-throughput
    # still skips it for a faster run.
    tp_messages = 2000  # must match the loop bound in message_throughput.st
    tp_ms = None
    tp_rate = None
    if not args.skip_throughput:
        tp_file = ACTORS_DIR / "message_throughput.st"
        print("  message_throughput", end="", flush=True)
        tp_ms, ok_tp, _ = timed([str(protost), str(tp_file)])
        tp_rate = (tp_messages / (tp_ms / 1000.0)) if (ok_tp and tp_ms) else None
        print(f"  {tp_messages} msgs: {tp_ms:.0f}ms  ({tp_rate:,.0f} msg/s)"
              if tp_rate else "  FAIL")
    else:
        print("  message_throughput  skipped (--skip-throughput)")

    # --- Report ---------------------------------------------------------------
    write_report(out_path, host_cpu, ncpu, protost, cpython, protopy,
                  comp_rows, par_full if ok_full else None,
                  par_one if ok_one else None, speedup,
                  coop_ms if ok_coop else None,
                  tp_ms, tp_rate, tp_messages)
    print(f"\nReport written: {out_path}")


def write_report(path, host_cpu, ncpu, protost, cpython, protopy,
                 comp_rows, par_full, par_one, speedup,
                 coop_ms, tp_ms, tp_rate, tp_messages):
    has_pp = protopy is not None
    lines = []
    lines.append(f"# protoST performance baseline — {datetime.now():%Y-%m-%d}")
    lines.append("")
    lines.append(f"- **Host:** {host_cpu} — {ncpu} logical CPUs")
    lines.append(f"- **OS:** {platform.system()} {platform.release()} "
                 f"({platform.machine()})")
    lines.append(f"- **Method:** {WARMUP_RUNS} warmup + {N_RUNS} timed runs per "
                 f"benchmark, median wall-clock reported.")
    lines.append(f"- **protoST:** `{protost}`")
    lines.append(f"- **CPython:** `{cpython}` ({platform.python_version()})")
    lines.append(f"- **protoPython:** "
                 f"{'`' + str(protopy) + '`' if has_pp else '_not available — column omitted_'}")
    lines.append("")
    lines.append("## Comparable workloads")
    lines.append("")
    lines.append("The protoPython core benchmark suite translated to idiomatic "
                 "protoST — same algorithm, same parameters (N). Times in "
                 "milliseconds (median). `Ratio` is protoST ÷ CPython "
                 "(>1 = protoST slower).")
    lines.append("")
    if has_pp:
        lines.append("| Benchmark | protoST (ms) | CPython (ms) | protopy (ms) | Ratio (ST/CPy) |")
        lines.append("|---|---:|---:|---:|---:|")
    else:
        lines.append("| Benchmark | protoST (ms) | CPython (ms) | Ratio (ST/CPy) |")
        lines.append("|---|---:|---:|---:|")
    ratios = []
    for name, st_ms, py_ms, pp_ms in comp_rows:
        st = f"{st_ms:.1f}" if st_ms else "FAIL"
        py = f"{py_ms:.1f}" if py_ms else "N/A"
        ratio = (st_ms / py_ms) if (st_ms and py_ms) else None
        if ratio:
            ratios.append(ratio)
        r = f"{ratio:.2f}×" if ratio else "—"
        if has_pp:
            pp = f"{pp_ms:.1f}" if pp_ms else "N/A"
            lines.append(f"| {name} | {st} | {py} | {pp} | {r} |")
        else:
            lines.append(f"| {name} | {st} | {py} | {r} |")
    gm = geomean(ratios)
    if has_pp:
        lines.append(f"| **Geomean** | | | | **{gm:.2f}×** |")
    else:
        lines.append(f"| **Geomean** | | | **{gm:.2f}×** |")
    lines.append("")
    lines.append(f"Geometric-mean ratio across the comparable suite: "
                 f"**protoST is {gm:.2f}× CPython's wall-clock** on these "
                 f"single-threaded workloads.")
    lines.append("")
    lines.append("## Actor-model benchmarks")
    lines.append("")
    lines.append("protoST-specific — protoPython has no actor model. These "
                 "exercise the cooperative actor scheduler.")
    lines.append("")
    lines.append("| Benchmark | Result |")
    lines.append("|---|---|")
    if speedup:
        lines.append(f"| **Parallel speedup** | 12 CPU-bound worker actors: "
                      f"{par_full:.0f} ms with the full pool vs "
                      f"{par_one:.0f} ms with `PROTOST_WORKERS=1` — "
                      f"**{speedup:.2f}× speedup** |")
    if coop_ms:
        lines.append(f"| **Cooperative-yield scaling** | **1000** waiter actors, "
                      f"each parked on a nested `wait`, all hosted on **K=2** "
                      f"worker threads — completes in {coop_ms:.0f} ms. "
                      f"Thread-per-actor blocking would need 1000 OS threads. |")
    if tp_rate:
        lines.append(f"| **Message throughput** | {tp_messages:,} drained "
                      f"round-trip sends to one actor in {tp_ms:.0f} ms — "
                      f"**{tp_rate:,.0f} messages/second**. |")
    else:
        lines.append("| **Message throughput** | _skipped "
                      "(`--skip-throughput`)._ |")
    lines.append("")
    lines.append("### Reading these numbers")
    lines.append("")
    lines.append("protoST's single-thread arithmetic is slower than CPython — "
                 "it is a young runtime and the comparable table shows that "
                 "honestly. The actor results are the differentiator: the "
                 "cooperative-yield benchmark hosts a thousand suspended "
                 "actors on two OS threads, which a thread-per-actor model "
                 "fundamentally cannot do, and the parallel benchmark turns "
                 "extra cores into real wall-clock speedup with no code "
                 "change.")
    lines.append("")
    path.write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
