# protoST on protoCore 2.14.8: the send path and inlined blocks (2026-10-20)

`benchmarks/run_benchmarks.py`, three rounds, protoST `main` at `3d19449`
(the merge of `perf/send-path` and `perf/inline-captures`), Release, built
against protoCore 2.14.8; each round behind the quiet gate (busy CPUs 1.2
at the start of round 1) and on a fresh copy of the library. CPython 3.14.0
on the same machine (AMD Ryzen 5 5500U, 6 cores / 12 threads). The
baseline is the 2026-10-18 run of protoST 0.6.6 on protoCore 2.14.7
(protoCore's
[write-path report](https://github.com/numaes/protoCore/blob/master/docs/reports/2026-10-18-write-path.md),
`st_after_r1..3`), same harness, same machine. protoCore 2.14.8 changes
only how a context's slots are allocated, which helps embedders that
allocated them on the heap per call (protoClojure, protoScala; measured in
protoCore's 2026-10-19 report), so most of the difference is the two
protoST changes; this run does not separate the two. Medians of the
three rounds; the raw reports are in [`data/2026-10-20/`](data/2026-10-20/).

## What changed

- **Inlined blocks no longer capture** (`perf/inline-captures`). The
  bodies of `ifTrue:`, `whileTrue:` and `to:do:` that the compiler inlines
  were treated as capture scopes, so every temporary they touched was
  boxed in the method's mutable captured dictionary: a hash write and a
  hash read per access, in the hottest loops of most programs.
- **The send path** (`perf/send-path`). The engine's own markers
  (`__bc_ptr__`, `__captured__`, `__wrapped__`, ...) are read as own
  attributes instead of full-chain misses; no pins on interned symbols
  (perennial); frame ids come from a per-thread block instead of a global
  atomic; the bootstrap and the debugger are engine members.

## Comparable workloads

Whole-process time; the ratio is protoST ÷ CPython (> 1: protoST is
slower); the work ratio subtracts each runtime's measured start-up.

| Benchmark | protoST ms | before | CPython ms | Ratio | before | Work ratio | before |
|---|---:|---:|---:|---:|---:|---:|---:|
| `int_sum_loop` | 47.7 | 45.8 | 33.7 | 1.41× | 1.41× | 4.7× | 5.7× |
| `fib` | 89.9 | 358.9 | 41.9 | 2.15× | 8.91× | 4.9× | 30.2× |
| `list_append` | 47.1 | 48.6 | 32.0 | 1.51× | 1.66× | 12.3× | 36.2× |
| `str_concat` | 28.1 | 27.5 | 30.6 | 0.92× | 0.95× | 2.4× | — |
| `attr_lookup` | 54.9 | 158.6 | 42.6 | 1.28× | 4.16× | 2.3× | 15.6× |
| `range_iterate` | 35.8 | 72.3 | 34.4 | 1.03× | 2.24× | 2.3× | 11.8× |
| `exception_latency` | 863.5 | 958.5 | 49.6 | 17.29× | 20.65× | 42.5× | 57.2× |
| **Geomean** | | | | **1.92×** | **3.25×** | | |

Start-up: 25.0 ms (24.5 before), CPython 31 ms.

## Actors

| Measure | Now | Before |
|---|---:|---:|
| `parallel_speedup`, default pool vs one worker | 2.63× | 2.05× |
| `saturation_big`, 2 workers vs 1 | 1.91× | 1.80× |
| 3 workers | 2.57× | 2.33× |
| 4 workers | 3.23× | 2.71× |
| 5 workers | 3.53× | 2.77× |
| 6 workers | 3.27× | 2.83× |

The speedups rise: the parallel times fell more than the single-worker
ones. Not measured here, but consistent with the send path's changes: the
global frame-id counter, an atomic every worker incremented on every call,
is gone. The 6-worker figure fell below the 5-worker one in every round of
this run, on a desktop with other work running; it is not a settled peak.

## Reading

- `fib` 4× faster, `attr_lookup` 2.9×, `range_iterate` 2×: the loops and
  recursions whose temporaries lived in the captured dictionary.
- `int_sum_loop`, `str_concat`, `list_append`: within the spread of the
  whole-process time (start-up is most of it at these N).
- `exception_latency` -10 %: still 17× CPython's time, the cost of
  C++ unwinding per signal (by design, `DESIGN.md`).
