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
| `message_throughput.st` | 2000 drained round-trip sends through one actor's mailbox; the harness reports messages/second. Runs by default; `--skip-throughput` opts out. (It was once opt-in because of D23, a scheduler deadlock under sustained mailbox load — now fixed; see `docs/STATUS.md`.) |
| `multi_producer.st` | Driver actors each fanning out sends to their own sink actors, then waiting on the Futures: multi-producer message throughput. |
| `saturation.st`, `saturation_big.st`, `saturation_8a.st`, `saturation_32a.st`, `saturation_128a.st` | Worker-pool drain capacity: mailboxes pre-filled while the pool is paused, then drained (`saturation.st`); `saturation_big.st` is the larger, allocation-bound variant (32 actors x 50 messages); the `_Na` variants vary the number of actors against the number of workers. |
| `mailbox_send_cost.st`, `mailbox_send_contended.st`, `mailbox_send_headroom.st` | The cost of one mailbox send: uncontended with the pool stopped, eight producers into one sink, and a 200,000-message backlog. Run through `run_mailbox_bench.sh`, which refuses to start while another benchmark runs and verifies each program's self-reported work. |

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

Reports live in `reports/`, one dated file per run; the first is
`reports/2026-05-21-baseline.md`.
