#!/usr/bin/env python3
"""Interleaved comparison of protost configs (no PMU access: rusage only).
usage: perfbench.py OUT.json ROUNDS wl[,wl] LABEL=BIN[@LIBDIR][%ENV=V;ENV=V] ...
Every run must print a VERIFIED line and exit 0 (else abort). Before each run it
waits (up to QUIET_WAIT s) until no competitor process runs and load1 < QUIET_LOAD.
Per run: wall ms, ELAPSED_MS printed by the program (the measured section),
user and sys CPU ms, voluntary / involuntary context switches, minor faults,
peak RSS. Prints medians."""
import json, os, re, subprocess, sys, tempfile, time, statistics
ST = "/home/gamarino/Documentos/proyectos/protoST"
W = {
 "sat1": (["benchmarks/actors/saturation_big.st"], {"PROTOST_WORKERS": "1"}),
 "par1": (["benchmarks/actors/parallel_speedup.st"], {"PROTOST_WORKERS": "1"}),
 "coop2": (["benchmarks/actors/cooperative_yield.st"], {"PROTOST_WORKERS": "2"}),
 "fib": (["benchmarks/comparable/fib.st"], {}),
}
COMP = re.compile(r"^(cc1plus|cc1|ctest|lto1|ld|make|ninja|gmake|protopy|protoJS|protojs|protost|protoclj|protoscala|test262|node)$")
QL = float(os.environ.get("QUIET_LOAD", "3.5")); QW = float(os.environ.get("QUIET_WAIT", "60"))
KEYS = ("wall_ms", "elapsed_ms", "user_ms", "sys_ms", "nvcsw", "nivcsw", "minflt", "maxrss_mb")

def competitors():
    out = subprocess.run(["ps", "-eo", "comm="], capture_output=True, text=True).stdout.split()
    return sorted({n for n in out if COMP.search(n)})

def wait_quiet():
    t0 = time.time()
    while time.time() - t0 < QW:
        if not competitors() and os.getloadavg()[0] < QL:
            return True
        time.sleep(5)
    return False

def run(cfg, wl):
    label, binary, lib, extra = cfg
    tail, env = W[wl]
    quiet = wait_quiet()
    e = dict(os.environ); e.update(env); e.update(extra); e["LC_ALL"] = "C"
    if lib:
        e["LD_LIBRARY_PATH"] = lib
    comp = competitors(); load = os.getloadavg()[0]
    with tempfile.TemporaryFile("w+") as fo, tempfile.TemporaryFile("w+") as fe:
        t0 = time.perf_counter()
        p = subprocess.Popen([binary] + tail, cwd=ST, env=e, stdout=fo, stderr=fe,
                             stdin=subprocess.DEVNULL)
        _, status, ru = os.wait4(p.pid, 0)
        wall = (time.perf_counter() - t0) * 1000
        p.returncode = os.waitstatus_to_exitcode(status)
        fo.seek(0); fe.seek(0); so = fo.read(); se = fe.read()
    lines = so.strip().splitlines()
    if p.returncode != 0 or not lines or not lines[-1].startswith("VERIFIED"):
        sys.exit(f"FAILED {label} {wl}: rc={p.returncode} out={so[-300:]} err={se[-500:]}")
    el = [l for l in lines if "ELAPSED_MS" in l]
    return {"wall_ms": wall,
            "elapsed_ms": float(re.findall(r"\d+", el[0])[0]) if el else wall,
            "user_ms": ru.ru_utime * 1000, "sys_ms": ru.ru_stime * 1000,
            "nvcsw": ru.ru_nvcsw, "nivcsw": ru.ru_nivcsw, "minflt": ru.ru_minflt,
            "maxrss_mb": ru.ru_maxrss / 1024, "load": load, "competitors": comp,
            "quiet": quiet}

def main():
    out, rounds, wls = sys.argv[1], int(sys.argv[2]), sys.argv[3].split(",")
    cfgs = []
    for a in sys.argv[4:]:
        label, rest = a.split("=", 1); extra = {}
        if "%" in rest:
            rest, ev = rest.split("%", 1)
            extra = dict(kv.split("=", 1) for kv in ev.split(";") if kv)
        binary, lib = (rest.split("@", 1) + [None])[:2]
        cfgs.append((label, binary, lib, extra))
    res = {wl: {c[0]: [] for c in cfgs} for wl in wls}
    for wl in wls:
        for c in cfgs:
            run(c, wl)  # warm-up, discarded
        for r in range(rounds):
            order = cfgs[r % len(cfgs):] + cfgs[:r % len(cfgs)]
            for c in order:
                res[wl][c[0]].append(run(c, wl))
        json.dump({"argv": sys.argv, "results": res}, open(out, "w"), indent=1)
        print(f"== {wl}"); base = None
        for c in cfgs:
            rs = res[wl][c[0]]
            med = {k: statistics.median([x[k] for x in rs]) for k in KEYS}
            if base is None:
                base = med
            pct = lambda k: 100 * (med[k] / base[k] - 1) if base[k] else 0.0
            print(f"{c[0]:>20}: wall {med['wall_ms']:6.0f} ({pct('wall_ms'):+5.1f}%) "
                  f"elapsed {med['elapsed_ms']:6.0f} ({pct('elapsed_ms'):+5.1f}%) "
                  f"user {med['user_ms']:6.0f} ({pct('user_ms'):+5.1f}%) sys {med['sys_ms']:5.0f} "
                  f"vcsw {med['nvcsw']:6.0f} ivcsw {med['nivcsw']:5.0f} minflt {med['minflt']:7.0f} "
                  f"rss {med['maxrss_mb']:4.0f}MB | load<= {max(x['load'] for x in rs):.1f} "
                  f"notquiet {sum(not x['quiet'] for x in rs)}/{len(rs)} "
                  f"comp {sorted(set(sum((x['competitors'] for x in rs), [])))}", flush=True)

main()
