# protoST benchmarks — 2026-10-22

- **Host:** AMD Ryzen 5 5500U with Radeon Graphics, 12 logical CPUs, Linux 7.0.0-38-generic
- **Load average (1 min):** 7.97 before, 8.33 after
- **protoST:** protoST 0.6.6 at commit `b8b71b3`
- **CPython:** Python 3.14.0
- **Method:** 2 warmup + 5 timed runs per measurement; median wall-clock, min–max in parentheses. Every run verified its result (`VERIFIED` / `BENCH_RESULT`); a failed run fails the harness.

## Start-up

A process that evaluates one expression: protoST 36.8 ms (35.6–39.6), CPython 56.7 ms (41.4–75.4). The comparable workloads below use small N, so start-up is part of every time.

## Comparable workloads

Same algorithm, same N and same verified result as the protoPython twin. `Ratio` is protoST ÷ CPython on whole-process time (>1 means protoST is slower). At these N the process start-up is a large share of every time, so `Work ratio` also compares the times with each runtime's measured start-up subtracted: the cost of the work itself.

| Benchmark | N | protoST ms | CPython ms | Ratio | Work ratio |
|---|---:|---:|---:|---:|---:|
| int_sum_loop | 100000 | 66.2 (57.0–69.8) | 68.5 (63.8–72.2) | 0.97× | 2.5× |
| fib | 25 | 128.5 (120.9–134.3) | 92.1 (85.3–107.0) | 1.40× | 2.6× |
| list_append | 10000 | 89.3 (78.2–99.9) | 55.2 (44.5–69.0) | 1.62× | — |
| str_concat | 2000 | 47.2 (42.0–53.5) | 64.6 (55.8–68.9) | 0.73× | 1.3× |
| attr_lookup | 100000 | 79.2 (72.8–108.1) | 93.2 (77.1–101.1) | 0.85× | 1.2× |
| range_iterate | 100000 | 61.3 (49.7–64.6) | 56.4 (50.4–69.0) | 1.09× | — |
| exception_latency | 50000 | 1276.9 (1236.5–1377.5) | 68.1 (59.8–86.9) | 18.75× | 108.5× |
| **Geomean** | | | | **1.61×** | **4.0×** |

## Actors

- **Parallel speedup** (12 CPU-bound actors): 290.1 ms with one worker, 121.8 ms with the default pool: **2.38×**.
- **Cooperative yield:** 1000 actors, each parked on a `wait`, on 2 worker threads: 268.1 ms.
- **Message throughput:** 2000 round-trip sends (send + `wait`): 102.2 ms, about 19,570 round trips per second including start-up.

Worker scaling of `saturation_big.st` (32 actors × 50 messages of CPU work):

| Workers | ms | Speedup vs 1 |
|---:|---:|---:|
| 1 | 1242.9 (1182.4–1527.7) | 1.00× |
| 2 | 666.1 (623.7–722.8) | 1.87× |
| 3 | 579.2 (503.1–601.5) | 2.15× |
| 4 | 553.9 (549.8–699.3) | 2.24× |
| 5 | 554.0 (437.7–573.3) | 2.24× |
| 6 | 466.3 (438.3–564.7) | 2.67× |

