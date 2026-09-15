# protoST performance baseline — 2026-05-24

- **Host:** AMD Ryzen 5 5500U with Radeon Graphics — 12 logical CPUs
- **OS:** Linux 6.8.0-117-generic (x86_64)
- **Method:** 2 warmup + 5 timed runs per benchmark, median wall-clock reported.
- **protoST:** `build/protost`
- **CPython:** `/usr/local/bin/python3.14` (3.14.0)
- **protoPython:** `protoPython/build_release/src/runtime/protopy`

## Comparable workloads

The protoPython core benchmark suite translated to idiomatic protoST — same algorithm, same parameters (N). Times in milliseconds (median). `Ratio` is protoST ÷ CPython (>1 = protoST slower).

| Benchmark | protoST (ms) | CPython (ms) | protopy (ms) | Ratio (ST/CPy) |
|---|---:|---:|---:|---:|
| int_sum_loop | 38.7 | 39.6 | 22.4 | 0.98× |
| fib | 405.4 | 41.3 | 133.1 | 9.82× |
| list_append | 59.9 | 30.8 | 301.8 | 1.94× |
| str_concat | 34.2 | 44.7 | 540.4 | 0.77× |
| attr_lookup | 103.4 | 40.4 | 259.7 | 2.56× |
| range_iterate | 49.1 | 34.6 | 223.8 | 1.42× |
| exception_latency | 1280.6 | 45.4 | 702.7 | 28.18× |
| **Geomean** | | | | **2.83×** |

Geometric-mean ratio across the comparable suite: **protoST is 2.83× CPython's wall-clock** on these single-threaded workloads.

## Actor-model benchmarks

protoST-specific — protoPython has no actor model. These exercise the cooperative actor scheduler.

| Benchmark | Result |
|---|---|
| **Parallel speedup** | 12 CPU-bound worker actors: 359 ms with the full pool vs 665 ms with `PROTOST_WORKERS=1` — **1.85× speedup** |
| **Cooperative-yield scaling** | **1000** waiter actors, each parked on a nested `wait`, all hosted on **K=2** worker threads — completes in 1176 ms. Thread-per-actor blocking would need 1000 OS threads. |
| **Message throughput** | 2,000 drained round-trip sends to one actor in 36 ms — **55,958 messages/second**. |

### Reading these numbers

protoST's single-thread arithmetic is slower than CPython — it is a young runtime and the comparable table shows that honestly. The actor results are the differentiator: the cooperative-yield benchmark hosts a thousand suspended actors on two OS threads, which a thread-per-actor model fundamentally cannot do, and the parallel benchmark turns extra cores into real wall-clock speedup with no code change.

