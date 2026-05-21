# protoST benchmarks

The protoST performance benchmark suite (Roadmap **Track 11**). Two families:

## 1. Comparable workloads — `comparable/`

The protoPython core benchmark suite translated to idiomatic protoST. Each
`.st` file runs the **same algorithm with the same parameters (N)** as its
protoPython `.py` twin in `../../protoPython/benchmarks/`, so the harness can
place protoST, CPython and protoPython side by side.

| protoST file | Workload | protoPython twin |
|---|---|---|
| `int_sum_loop.st` | sum 1..100000 | `int_sum_loop.py` |
| `fib.st` | recursive `fib(25)` | `call_recursion.py` |
| `list_append.st` | 10000 appends to an `OrderedCollection` | `list_append_loop.py` |
| `str_concat.st` | 2000 string concatenations | `str_concat_loop.py` |
| `attr_lookup.st` | 100000 × 3 instance-variable reads | `attr_lookup.py` |
| `range_iterate.st` | iterate 1..100000 counting | `range_iterate.py` |
| `exception_latency.st` | 50000 iterations, signal+`on:do:` on even i | `exception_latency.py` |

Each file is self-checking — the leading comment states the expected result.

## 2. Actor-model benchmarks — `actors/`

protoST-specific. protoPython has no actor model, so these have no Python
column; they showcase protoST's distinctive feature.

| File | What it measures |
|---|---|
| `parallel_speedup.st` | 12 CPU-bound worker actors — wall-clock with the full worker pool vs `PROTOST_WORKERS=1`; the harness reports the speedup. |
| `cooperative_yield.st` | 1000 waiter actors, each parked on a nested `wait`, all hosted on K=2 worker threads — what thread-per-actor blocking cannot do. |
| `message_throughput.st` | 2000 messages through one actor's mailbox; the harness reports messages/second. **Opt-in** (`--with-throughput`) — it exercises a known non-deterministic actor-scheduler deadlock (`docs/STATUS.md` D23) and is kept out of the default timed path; the file is retained as the bug's repro. |

## Running

```bash
cmake -B build -S . && cmake --build build -j     # build protost first
benchmarks/run.sh                                 # run everything
# or directly:
python3 benchmarks/run_benchmarks.py --output benchmarks/reports/NAME.md
```

The harness does `WARMUP_RUNS` warmup + `N_RUNS` timed runs per benchmark
(same discipline as protoPython's harness), reports the median wall-clock and a
geometric mean, and writes a dated markdown report to `reports/`.

Environment variables: `PROTOST_BIN` (default `./build/protost`), `CPYTHON_BIN`
(default `python3`), `PROTOPY_BIN` (autodetected from `../protoPython/build*`;
the protopy column is skipped if no binary is found — the run does not fail).

Reports live in `reports/`; see `reports/2026-05-21-baseline.md`.
