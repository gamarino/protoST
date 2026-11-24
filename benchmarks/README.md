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

## 3. The constant-read gate — `const_read_gate.py`

The performance gate of the explicit-state spec (§14.1) for protoST 0.17.0. It
measures, in CPU cycles, three programs on the reference build and the
candidate:

| Program | What it is |
|---|---|
| startup | an empty `-e` program: each build's startup, reported so that the loop cost can be read with startup subtracted |
| `comparable/attr_lookup.st` | 100000 × 3 plain instance-variable reads |
| `micro/const_read.st` | its exact twin, with the three variables declared `constantNames: 'a b c'` and set by the generated constructor `FastObject a: 1 b: 2 c: 3`; 0.16.1 cannot parse it, so on the old build it is reported as "not runnable" |

Both bounds bind, and the script exits 1 when either fails:

- `attr_lookup` on the candidate is within 1 % of the reference (plain reads unchanged);
- `const_read` on the candidate is within 3 % of `attr_lookup` on the candidate (the check is the whole difference).

The startup-subtracted ratios are printed as information. For every
measurement the script refuses to start while a compiler, a test runner or a
protoCore runtime is running; copies the binary and `libprotoCore.so*` into a
directory that never existed before (library placement alone moves timings);
checks with `ldd` that the copy is the library loaded; runs
`perf stat -r 3 -x, -e cycles` with that directory on `LD_LIBRARY_PATH` and
the build's own `lib/` on `PROTOST_LIB`; and checks that every one of the
three runs printed its `VERIFIED` line. A measurement that cannot be made or
verified makes it exit 2.

```bash
python3 benchmarks/const_read_gate.py \
    --old /path/to/0.16.1/build_release/protost --new build_release/protost \
    --libdir /path/to/protoCore-3.6.1/lib --json out.json \
    --runs-root /path/to/scratch/gate-runs [--rounds 3]
```

`--rounds N` repeats every measurement N times with fresh copies, alternating
the build order, and gates on the mean. `--extra NAME=PATH=MARKER` measures
another program the same way, reported and not gated (`--only-extras` skips
the gate programs); `micro/instances.st`, `micro/vm_ops.st`,
`micro/point_new.st` and `micro/fraction_new.st` are measured that way. The
0.17.0 figures are in `reports/2026-11-23-release-0.17.0-const-read.md`.

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

To compare builds (another protoST, or the same one against another
protoCore), `compare_builds.py` runs the same workloads interleaved: one
warm-up round and three measured rounds, every build once per round in a
rotating order, each run verified as above, with the peak RSS of every run and
a note of any compiler, test runner or protoCore runtime that was running when
it started:

```bash
python3 benchmarks/compare_builds.py --json out.json \
    old=/usr/bin/protost new=build_release/protost
```

To compare one binary against two protoCore libraries, list it twice and
give each label its library directory; `ldd` is checked before every run:

```bash
python3 benchmarks/compare_builds.py --json out.json \
    old=/usr/bin/protost new=/usr/bin/protost --libdir new=/path/to/protoCore/lib
```

Reports live in `reports/`, one dated file per run; the first is
`reports/2026-05-21-baseline.md`, the latest
`reports/2026-10-12-protocore-2.14.6.md` (raw data in `reports/data/`).
