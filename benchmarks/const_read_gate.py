#!/usr/bin/env python3
"""
The explicit-state spec §14.1 performance gate: the cost of the constant-read
check, measured in CPU cycles.

Programs:
  startup      an empty program (`-e`), the per-build startup baseline (C19);
  attr_lookup  benchmarks/comparable/attr_lookup.st, 100000 x 3 plain reads;
  const_read   benchmarks/micro/const_read.st, its exact twin with the three
               variables declared `constantNames:`. 0.16.1 cannot parse it
               (ruling G3): on the old build it is run once, unmeasured, and
               reported as "not runnable".

For every (build, program) measurement the script:
  1. refuses to start while a compiler, a test runner or another protoCore
     runtime is running (competitors_running, from compare_builds.py);
  2. copies the binary and libprotoCore.so* into a fresh directory
     RUNS_ROOT/<n>, never reusing one (library placement alone moves timings);
  3. checks with `ldd` that the copy resolves the copied libprotoCore;
  4. runs `perf stat -r REPEAT -x, -e cycles` with LD_LIBRARY_PATH set to that
     directory, PROTOST_LIB set to the build's own standard library and the
     working directory set to the fresh directory;
  5. checks that every one of the REPEAT runs printed its VERIFIED line.

It reports perf's mean cycles per program and checks both bounds:
  attr_lookup(new) <= 1.01 x attr_lookup(old)
  const_read(new)  <= 1.03 x attr_lookup(new)
It also reports the startup-subtracted ratios (C19). The bounds bind; the
subtracted ratios are information. It exits 1 on a breach and 2 when a
measurement could not be made or verified.

--extra NAME=PATH=MARKER measures one more program on both builds the same
way (the last output line of every run must be MARKER), reported and not
gated; --only-extras skips the gate programs.

Usage:
  python3 benchmarks/const_read_gate.py --old OLD/protost --new NEW/protost \\
      --libdir PROTOCORE/lib --json OUT.json [--runs-root DIR] [--rounds 1]
"""

import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path

from compare_builds import competitors_running

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent
TIMEOUT = 600  # seconds per perf invocation
ATTR_BOUND = 1.01
CONST_BOUND = 1.03

GATE_PROGRAMS = [
    ("startup", ["-e", "'VERIFIED ' , 42 printString"], "VERIFIED 42"),
    ("attr_lookup", [str(PROJECT_ROOT / "benchmarks/comparable/attr_lookup.st")], "VERIFIED 600000"),
    ("const_read", [str(PROJECT_ROOT / "benchmarks/micro/const_read.st")], "VERIFIED 600000"),
]


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def next_run_dir(root):
    """A directory under root that has never existed: the next free integer."""
    root.mkdir(parents=True, exist_ok=True)
    n = 1 + max((int(p.name) for p in root.iterdir() if p.name.isdigit()), default=0)
    d = root / str(n)
    d.mkdir()
    return d


def fresh_copy(build, libdir, root):
    """Copy the binary and every libprotoCore.so* (symlinks kept) into a new directory."""
    d = next_run_dir(root)
    shutil.copy2(build["path"], d / "protost")
    copied = []
    for lib in sorted(Path(libdir).glob("libprotoCore.so*")):
        shutil.copy2(lib, d / lib.name, follow_symlinks=False)
        copied.append(lib.name)
    if not copied:
        sys.exit(f"no libprotoCore.so* in {libdir}")
    return d


def resolved_core(binary, env):
    out = subprocess.run(["ldd", str(binary)], capture_output=True, text=True, env=env).stdout
    line = next((l.strip() for l in out.splitlines() if "libprotoCore" in l), "")
    return line.split(" (0x")[0]


def run_env(run_dir, stdlib):
    env = {**os.environ, "LD_LIBRARY_PATH": str(run_dir), "PROTOST_LIB": str(stdlib),
           "LC_ALL": "C"}
    return env


def parse_perf_csv(text):
    """(mean cycles, relative stddev %) from `perf stat -x,` output."""
    for line in text.splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        f = line.split(",")
        if len(f) > 3 and f[2].startswith("cycles"):
            if not f[0].strip().isdigit():
                raise ValueError(f"cycles not counted: {line!r}")
            var = f[3].strip()  # "+-x%" only with -r > 1; otherwise the run time
            return int(f[0]), (float(var.rstrip("%")) if var.endswith("%") else None)
    raise ValueError(f"no cycles line in perf output: {text!r}")


def refuse(why):
    """A measurement that must not be made: exit 2, as a failed one does."""
    print(why, file=sys.stderr)
    sys.exit(2)


class MeasureError(Exception):
    pass


def measure(build, name, tail, marker, args, log):
    contenders = competitors_running()
    if contenders:
        refuse(f"refusing to measure {name} [{build['label']}]: running {contenders}")
    run_dir = fresh_copy(build, args.libdir, args.runs_root)
    binary = run_dir / "protost"
    env = run_env(run_dir, build["stdlib"])
    core = resolved_core(binary, env)
    real = Path(core.split("=>")[1].strip()).resolve() if "=>" in core else None
    if (real is None or real.parent != run_dir
            or not real.name.endswith(".so." + args.core_version)):
        raise MeasureError(f"{name} [{build['label']}]: ldd resolves {core!r} -> {real}, not the "
                           f"copied libprotoCore {args.core_version} in {run_dir}")
    perf_out = run_dir / "perf.csv"
    load = os.getloadavg()
    p = subprocess.run(["perf", "stat", "-r", str(args.repeat), "-x,", "-e", "cycles",
                        "-o", str(perf_out), "--", str(binary)] + tail,
                       cwd=run_dir, env=env, capture_output=True, text=True, timeout=TIMEOUT)
    (run_dir / "stdout.txt").write_text(p.stdout)
    (run_dir / "stderr.txt").write_text(p.stderr)
    lines = [l.strip() for l in p.stdout.splitlines() if l.strip()]
    verified = sum(1 for l in lines if l == marker)
    if p.returncode != 0 or verified != args.repeat or (lines and lines[-1] != marker):
        raise MeasureError(f"{name} [{build['label']}]: exit {p.returncode}, {verified} of "
                           f"{args.repeat} runs printed {marker!r}; stderr: {p.stderr.strip()[:300]}")
    cycles, var = parse_perf_csv(perf_out.read_text())
    rec = {"program": name, "build": build["label"], "run_dir": str(run_dir),
           "protoCore": core, "protoCore_file": str(real), "cycles": cycles, "stddev_pct": var, "runs": args.repeat,
           "verified": f"{verified} x {marker}", "load_before": [round(x, 2) for x in load],
           "load_after": [round(x, 2) for x in os.getloadavg()],
           "time": datetime.now().isoformat(timespec="seconds")}
    log.append(rec)
    print(f"  {name:<22} {build['label']:<4} {cycles:>14,} cycles  (+-{var}%)  {run_dir}", flush=True)
    return rec


def not_runnable_check(build, name, tail, args):
    """Run once, unmeasured, to record why a program cannot run on a build (G3)."""
    run_dir = fresh_copy(build, args.libdir, args.runs_root)
    p = subprocess.run([str(run_dir / "protost")] + tail, cwd=run_dir,
                       env=run_env(run_dir, build["stdlib"]), capture_output=True, text=True,
                       timeout=TIMEOUT)
    out = (p.stdout + p.stderr).strip()
    runnable = p.returncode == 0 and "VERIFIED 600000" in p.stdout
    status = "runnable" if runnable else "not runnable"
    print(f"  {name:<22} {build['label']:<4} {status}: exit {p.returncode}; "
          f"{out.splitlines()[0] if out else ''}", flush=True)
    return {"program": name, "build": build["label"], "status": status,
            "exit": p.returncode, "output": out[:2000]}


def mean(xs):
    return sum(xs) / len(xs)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--old", required=True, help="the reference protost (0.16.1)")
    ap.add_argument("--new", required=True, help="the candidate protost")
    ap.add_argument("--libdir", required=True, help="directory holding libprotoCore.so*")
    ap.add_argument("--json", required=True)
    ap.add_argument("--old-stdlib", help="old build's lib/ (default: <binary>/../../lib)")
    ap.add_argument("--new-stdlib", help="new build's lib/ (default: <binary>/../../lib)")
    ap.add_argument("--runs-root", default=None,
                    help="where the fresh copies go (default: ./gate-runs under $TMPDIR)")
    ap.add_argument("--repeat", type=int, default=3, help="perf stat -r")
    ap.add_argument("--rounds", type=int, default=1,
                    help="measure every (build, program) this many times, rotating the build order")
    ap.add_argument("--core-version", default="3.6.1",
                    help="the libprotoCore version the copy must resolve to")
    ap.add_argument("--extra", action="append", default=[], help="NAME=PATH=MARKER")
    ap.add_argument("--only-extras", action="store_true")
    args = ap.parse_args()
    args.libdir = str(Path(args.libdir).resolve())
    args.runs_root = Path(args.runs_root or Path(os.environ.get("TMPDIR", "/tmp")) / "gate-runs").resolve()

    contenders = competitors_running()
    if contenders:
        refuse(f"refusing to start: running {contenders}")

    builds = []
    for label, path, stdlib in (("old", args.old, args.old_stdlib), ("new", args.new, args.new_stdlib)):
        path = str(Path(path).resolve())
        stdlib = Path(stdlib).resolve() if stdlib else Path(path).parent.parent / "lib"
        if not (stdlib / "kernel" / "00-manifest.txt").is_file():
            sys.exit(f"{label}: no kernel manifest under {stdlib}")
        builds.append({"label": label, "path": path, "sha256": sha256(path), "stdlib": str(stdlib)})

    programs = [] if args.only_extras else list(GATE_PROGRAMS)
    for spec in args.extra:
        name, path, marker = spec.split("=", 2)
        programs.append((name, [str(Path(path).resolve())], marker))

    results = {"date": datetime.now().isoformat(timespec="seconds"), "host": platform.node(),
               "kernel": platform.release(), "repeat": args.repeat, "rounds": args.rounds,
               "libdir": args.libdir, "builds": builds, "not_runnable": [], "measurements": []}
    try:
        for b in builds:
            env = run_env(Path(args.libdir), b["stdlib"])
            b["version"] = subprocess.run([b["path"], "--version"], capture_output=True,
                                          text=True, env=env).stdout.strip()
        if not args.only_extras:
            results["not_runnable"].append(
                not_runnable_check(builds[0], "const_read", GATE_PROGRAMS[2][1], args))
            if results["not_runnable"][0]["status"] == "runnable":
                print("  note: const_read runs on the old build; it is measured there too")
        old_skip = {"const_read"} if (not args.only_extras
                                       and results["not_runnable"][0]["status"] != "runnable") else set()
        for rnd in range(args.rounds):
            print(f"round {rnd + 1}/{args.rounds}", flush=True)
            for name, tail, marker in programs:
                order = builds if rnd % 2 == 0 else builds[::-1]
                for b in order:
                    if b["label"] == "old" and name in old_skip:
                        continue
                    measure(b, name, tail, marker, args, results["measurements"])
    except MeasureError as e:
        results["error"] = str(e)
        Path(args.json).write_text(json.dumps(results, indent=1))
        print(f"\nMEASUREMENT FAILURE: {e}")
        sys.exit(2)

    summary = {}
    for m in results["measurements"]:
        summary.setdefault(m["program"], {}).setdefault(m["build"], []).append(m["cycles"])
    results["mean_cycles"] = {p: {b: round(mean(v)) for b, v in bv.items()} for p, bv in summary.items()}
    print("\nmean cycles (perf -r %d, %d round(s))" % (args.repeat, args.rounds))
    for p, bv in results["mean_cycles"].items():
        print(f"  {p:<22} " + "  ".join(f"{b} {v:>14,}" for b, v in bv.items()))

    breach = False
    if not args.only_extras:
        mc = results["mean_cycles"]
        a_old, a_new, c_new = mc["attr_lookup"]["old"], mc["attr_lookup"]["new"], mc["const_read"]["new"]
        s_old, s_new = mc["startup"]["old"], mc["startup"]["new"]
        r_attr, r_const = a_new / a_old, c_new / a_new
        r_attr_sub = (a_new - s_new) / (a_old - s_old)
        r_const_sub = (c_new - s_new) / (a_new - s_new)
        ok_attr, ok_const = r_attr <= ATTR_BOUND, r_const <= CONST_BOUND
        ok_attr_sub, ok_const_sub = r_attr_sub <= ATTR_BOUND, r_const_sub <= CONST_BOUND
        results["gate"] = {
            "attr_lookup new/old": round(r_attr, 4), "bound attr": ATTR_BOUND, "attr ok": ok_attr,
            "const_read new / attr_lookup new": round(r_const, 4), "bound const": CONST_BOUND,
            "const ok": ok_const,
            "startup-subtracted attr new/old": round(r_attr_sub, 4),
            "startup-subtracted const/attr new": round(r_const_sub, 4),
            "startup-subtracted attr ok": ok_attr_sub, "startup-subtracted const ok": ok_const_sub,
            "verdicts differ": (ok_attr and ok_const) != (ok_attr_sub and ok_const_sub),
            "const_read old": results["not_runnable"][0]["status"],
        }
        print("\n§14.1 gate")
        print(f"  attr_lookup new/old            {r_attr:.4f}  (bound {ATTR_BOUND})  "
              f"{'ok' if ok_attr else 'BREACH'}")
        print(f"  const_read new / attr_lookup new {r_const:.4f}  (bound {CONST_BOUND})  "
              f"{'ok' if ok_const else 'BREACH'}")
        print(f"  startup-subtracted: attr {r_attr_sub:.4f}  const {r_const_sub:.4f}  "
              f"(information; verdicts differ: {results['gate']['verdicts differ']})")
        print(f"  const_read old: {results['not_runnable'][0]['status']} (G3)")
        breach = not (ok_attr and ok_const)
    Path(args.json).write_text(json.dumps(results, indent=1))
    if breach:
        print("\nGATE BREACH: the release is blocked until the maintainer accepts the figures.")
        sys.exit(1)
    print("\nAll runs verified." + ("" if args.only_extras else " Both bounds hold."))


if __name__ == "__main__":
    main()
