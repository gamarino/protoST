# protoST benchmarks — 2026-10-20

- **Host:** AMD Ryzen 5 5500U with Radeon Graphics, 12 logical CPUs, Linux 7.0.0-38-generic
- **Load average (1 min):** 9.10 before, 6.84 after
- **protoST:** protoST 0.6.6 at commit `3d19449`
- **CPython:** Python 3.14.0
- **Method:** 2 warmup + 5 timed runs per measurement; median wall-clock, min–max in parentheses. Every run verified its result (`VERIFIED` / `BENCH_RESULT`); a failed run fails the harness.

## Start-up

A process that evaluates one expression: protoST 23.9 ms (23.6–25.6), CPython 29.5 ms (28.8–35.0). The comparable workloads below use small N, so start-up is part of every time.

## Comparable workloads

Same algorithm, same N and same verified result as the protoPython twin. `Ratio` is protoST ÷ CPython on whole-process time (>1 means protoST is slower). At these N the process start-up is a large share of every time, so `Work ratio` also compares the times with each runtime's measured start-up subtracted: the cost of the work itself.

| Benchmark | N | protoST ms | CPython ms | Ratio | Work ratio |
|---|---:|---:|---:|---:|---:|
| int_sum_loop | 100000 | 50.0 (46.7–64.1) | 44.9 (34.3–58.4) | 1.11× | 1.7× |
| fib | 25 | 87.8 (85.0–90.2) | 42.4 (40.6–44.0) | 2.07× | 4.9× |
| list_append | 10000 | 46.9 (39.8–51.7) | 30.6 (30.3–35.1) | 1.53× | 19.9× |
| str_concat | 2000 | 27.4 (27.0–27.6) | 30.7 (29.1–33.3) | 0.89× | 2.8× |
| attr_lookup | 100000 | 54.5 (50.8–69.2) | 42.6 (40.3–43.9) | 1.28× | 2.3× |
| range_iterate | 100000 | 39.6 (36.8–43.1) | 33.7 (32.9–37.0) | 1.17× | 3.7× |
| exception_latency | 50000 | 845.9 (835.8–901.5) | 49.6 (47.3–50.7) | 17.04× | 40.8× |
| **Geomean** | | | | **1.87×** | **5.5×** |

## Actors

- **Parallel speedup** (12 CPU-bound actors): 236.2 ms with one worker, 89.8 ms with the default pool: **2.63×**.
- **Cooperative yield:** 1000 actors, each parked on a `wait`, on 2 worker threads: 217.9 ms.
- **Message throughput:** 2000 round-trip sends (send + `wait`): 83.4 ms, about 23,985 round trips per second including start-up.

Worker scaling of `saturation_big.st` (32 actors × 50 messages of CPU work):

| Workers | ms | Speedup vs 1 |
|---:|---:|---:|
| 1 | 1010.7 (983.7–1045.6) | 1.00× |
| 2 | 503.5 (492.6–530.4) | 2.01× |
| 3 | 358.9 (353.9–387.0) | 2.82× |
| 4 | 300.8 (296.2–359.6) | 3.36× |
| 5 | 263.9 (260.4–352.3) | 3.83× |
| 6 | 249.2 (240.8–339.7) | 4.06× |

