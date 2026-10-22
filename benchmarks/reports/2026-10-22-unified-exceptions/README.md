# protoST on protoCore's unified exception model: final measurement (2026-10-22)

## Builds

| | protoST | protoCore | Notes |
|---|---|---|---|
| **BEFORE** | `c0ba168` | `cf52f9f5` | 3.0.0 calling convention, no `ProtoException` |
| **AFTER** | `b8b71b3` (`feature/protocore-3-calling-convention` tip) | `65760e4a` (`feature/args-calling-convention` tip) | `2fdf371` port, `4ff0107`, `b8b71b3` |

- **Worktrees**: clean, detached, under
  `.agent_scratch/meas_1022/{before,after}/{protoCore,protoST,protoIO}`.
  - protoIO is a dependency of protoST. Both sides use the same protoIO commit,
    `1611ff9` (0.2.2), built as a sibling by `add_subdirectory`. It does not
    link protoCore.
- **Build**: Release, `-DCMAKE_DISABLE_FIND_PACKAGE_protoCore=ON`, so protoST
  uses the developer fallback: the sibling `../protoCore/build_release`.
  - `build3.sh` built everything first, one build at a time, at `-j4`.
  - Nothing was built during a measurement.
- **Library loading**: every run loaded a fresh copy of its own variant's
  `libprotoCore.so.4` through `LD_LIBRARY_PATH`. `ldd.txt` records the library
  that resolved for every run. No run resolved `/usr/lib`.
- **Quiet gate**: **`QUIET_CORES=4.0`** (`quiet_*.txt`).
  - The default of 2.5 cannot be reached on this desktop: the VS Code GPU
    process takes about 1 CPU, and Chrome and PyCharm 1-2 more.
  - At the gates, 3.4-3.9 CPUs were busy and the 1-minute load average was
    5.9-9.0.
  - **Only the interleaved BEFORE/AFTER ratios are meaningful.** The absolute
    figures are contended.
- **Self-report check** (`verify.txt`)
  - Coverage: all 7 comparables, the 4 actor benchmarks the harness runs, and
    the 6 probes. Each ran once per variant.
  - Result: 34/34 rc=0, each with the expected `VERIFIED`/`CHECK` value.
  - The harness also verifies every run itself. All 6 harness runs ended with
    "All runs verified.", rc=0.

## `benchmarks/run_benchmarks.py` (comparables + actors)

- **Method**
  - Three rounds, with the variant order alternated (A,B / B,A / A,B).
  - Each harness run does 2 warm-ups and 5 timed runs per cell.
  - The figures are the medians of the three per-round medians, in ms of
    whole-process wall time.
  - Both sides ran AFTER's copy of the harness. `benchmarks/` is identical in
    the two commits.
- **Comparison runtimes**
  - **CPython 3.14.0** is the harness's own comparable.
  - The protopy column was switched off (`PROTOPY_BIN=/nonexistent`). protoPython
    is measured in its own report.
  - **No Pharo is installed and the harness has no Pharo column**, so there is
    no Smalltalk reference.

| Workload | CPython | BEFORE | AFTER | A/B | BEFORE rounds | AFTER rounds |
|---|---:|---:|---:|---:|---|---|
| `startup` | 47.5 | 37.9 | 36.8 | 0.971 | 33/39/38 | 36/37/37 |
| `int_sum_loop` | 54.4 | 65.2 | 67.3 | 1.032 | 65/65/70 | 67/66/72 |
| `fib` | 63.4 | 117.0 | 116.1 | 0.992 | 116/117/118 | 116/128/114 |
| `list_append` | 48.7 | 67.2 | 75.5 | 1.124 | 67/66/68 | 68/89/76 |
| `str_concat` | 52.0 | 41.3 | 47.2 | 1.143 | 43/39/41 | 49/47/41 |
| `attr_lookup` | 65.2 | 74.5 | 75.6 | 1.015 | 74/73/115 | 75/79/76 |
| `range_iterate` | 53.1 | 54.2 | 57.0 | 1.052 | 54/56/52 | 57/61/50 |
| `exception_latency` | 71.3 | 1240.4 | 1238.2 | 0.998 | 1240/1226/1281 | 1210/1277/1238 |
| `parallel_speedup` (pool) | — | 140.8 | 128.9 | 0.915 | 146/141/132 | 133/122/129 |
| `parallel_speedup` (1 worker) | — | 306.0 | 290.1 | 0.948 | 306/299/334 | 309/290/272 |
| `cooperative_yield` | — | 254.3 | 268.1 | 1.054 | 253/254/258 | 326/268/256 |
| `message_throughput` | — | 101.9 | 113.9 | 1.118 | 98/102/118 | 139/102/114 |
| `saturation_big` w=1 | — | 1219.8 | 1133.3 | 0.929 | 1167/1220/1227 | 1133/1243/1131 |
| `saturation_big` w=2 | — | 698.4 | 663.3 | 0.950 | 680/705/698 | 663/666/657 |
| `saturation_big` w=3 | — | 521.5 | 512.5 | 0.983 | 522/660/520 | 500/579/512 |
| `saturation_big` w=4 | — | 465.6 | 479.4 | 1.030 | 429/645/466 | 479/554/426 |
| `saturation_big` w=5 | — | 478.5 | 407.2 | 0.851 | 478/457/513 | 407/554/387 |
| `saturation_big` w=6 | — | 474.7 | 423.8 | 0.893 | 446/542/475 | 412/466/424 |

Geomean, AFTER / BEFORE:

| Cells | Geomean |
|---|---:|
| comparables (7) | **1.049** |
| actor cells (10) | **0.964** |
| all 17 | **0.998** |

Against CPython 3.14 (7 comparables): BEFORE 1.74x, AFTER 1.83x.

### Reading it

- **The comparables' +4.9 % is not supported by a cycle count.**
  - The worst cells, `list_append` (+12 %) and `str_concat` (+14 %), are 5-8 ms
    on runs of 40-75 ms. Their AFTER rounds are spread wide (68/89/76).
  - A second pass with `perf stat -r 5` cycles, in `recheck/comparables.txt`,
    ran BEFORE/AFTER interleaved in both orders:

    | Workload | pass 1 A/B | pass 2 A/B |
    |---|---:|---:|
    | `list_append` | 0.90 | 1.02 |
    | `str_concat` | 1.00 | 0.99 |
    | `int_sum_loop` | 1.01 | 1.01 |
    | `range_iterate` | 1.01 | 0.98 |

  - **Verdict: noise, not a regression.** It is still a regression that was not
    excluded at the wall-clock level.
- **The actor cells vary ±15 % between rounds**, for example `saturation_big`
  w=4 and w=5. The 0.96 geomean is within that spread. Nothing in the actor
  path is claimed to be faster.
- **`exception_latency`**, the only harness workload that signals, is unchanged
  (0.998).

## Exception probes ([`probes/`](probes/), `probes.txt`)

- **Method**
  - `perf stat -r 3` cycles, BEFORE and AFTER interleaved per probe.
  - Each figure is cycles per iteration net of start-up: the cycles of
    `startup.st`, a one-line script, are subtracted, and the rest is divided by
    the iteration count. Each probe's `CHECK` line is its iteration count, and
    it was verified.
- **The probes**
  - All of them live in a method of class `Probe`. The handler is
    `on: Error do:` (`on: ZeroDivide do:` for `zerodiv`).
  - `throw_across5` signals from `deep: 0`, under 5 non-tail frames of
    `^ 1 + (self deep: d - 1)`.
- **The BEFORE commit ran every probe.** None needed a form that did not exist
  in it.

| Probe | iters | BEFORE | AFTER | A/B |
|---|---:|---:|---:|---:|
| `empty_loop` (`1 to: n do:`) | 2,000,000 | 383 | 359 | 0.94x |
| `try_no_throw` (`[..] on: Error do: [..]`, no signal) | 200,000 | 60,106 | 59,946 | 1.00x |
| `throw_catch` (`Error new signal:` caught in the same method) | 200,000 | 139,660 | 138,157 | 0.99x |
| `throw_across5` (signalled 5 frames down) | 100,000 | 112,462 | 116,473 | 1.04x |
| `zerodiv` (`1 // z` caught as `ZeroDivide`) | 200,000 | 148,923 | 145,054 | 0.97x |

- **The port does not change exception cost.** Every ratio is 0.94-1.04x, from
  single `-r 3` readings.
- **Unchanged by the port, and the largest finding: entering `on:do:` costs
  about 60,000 cycles even when nothing is signalled.**
  - That is 150x an empty loop iteration.
  - It is 43 % of the cost of a signal that is caught.
  - The probe first used 2,000,000 iterations and took about 40 s. It was cut
    to 200,000.
  - For comparison, an unraised `try` costs about 130 cycles in protoPython and
    about 400 in protoClojure (whole iteration).
- **Odd result: `throw_across5` (112k) is cheaper per iteration than
  `throw_catch` (140k).** The handler and the signal are the same; only the
  signalling site differs, a method five frames down against the protected
  block itself. This was not investigated.

## Not measured / deviations

- No Smalltalk reference (no Pharo on this machine).
- The probe figures are single `-r 3` readings, with no repeat pass. No probe
  moved by more than 5 %, so none was re-run.
- `QUIET_CORES=4.0` instead of the default 2.5 (see above).

## Files

- **Scripts**
  - `build3.sh`: the builds of all three runtimes.
  - `st_run.sh`: verification, harness and probes.
  - `recheck.sh`: the second pass, shared with protoScala and protoClojure.
  - `st_analyze.py` + `uex_common.py`: the tables.
- **Raw data**
  - `st_{before,after}_r{1,2,3}.{md,log}`
  - `probes.txt`, `recheck/comparables.txt`
  - `verify.txt`, `ldd.txt`, `quiet_*.txt`
  - `analysis.txt`: the full table, with the raw probe counters.
- **Worktrees**, kept: `/home/gamarino/Documentos/proyectos/.agent_scratch/meas_1022/{before,after}/protoST`,
  with builds in `build_release/`.
