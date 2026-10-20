# protoST benchmarks — 2026-10-20

- **Host:** AMD Ryzen 5 5500U with Radeon Graphics, 12 logical CPUs, Linux 7.0.0-38-generic
- **Load average (1 min):** 11.43 before, 9.10 after
- **protoST:** protoST 0.6.6 at commit `3d19449`
- **CPython:** Python 3.14.0
- **Method:** 2 warmup + 5 timed runs per measurement; median wall-clock, min–max in parentheses. Every run verified its result (`VERIFIED` / `BENCH_RESULT`); a failed run fails the harness.

## Start-up

A process that evaluates one expression: protoST 25.0 ms (24.3–27.9), CPython 31.2 ms (28.9–32.9). The comparable workloads below use small N, so start-up is part of every time.

## Comparable workloads

Same algorithm, same N and same verified result as the protoPython twin. `Ratio` is protoST ÷ CPython on whole-process time (>1 means protoST is slower). At these N the process start-up is a large share of every time, so `Work ratio` also compares the times with each runtime's measured start-up subtracted: the cost of the work itself.

| Benchmark | N | protoST ms | CPython ms | Ratio | Work ratio |
|---|---:|---:|---:|---:|---:|
| int_sum_loop | 100000 | 47.7 (46.8–50.2) | 33.7 (33.7–35.0) | 1.41× | 8.9× |
| fib | 25 | 89.9 (88.8–93.5) | 41.8 (41.4–43.0) | 2.15× | 6.1× |
| list_append | 10000 | 50.1 (49.7–57.5) | 33.2 (32.2–38.6) | 1.51× | 12.3× |
| str_concat | 2000 | 28.1 (27.1–28.5) | 30.6 (30.0–33.4) | 0.92× | — |
| attr_lookup | 100000 | 56.9 (56.3–58.8) | 42.2 (40.9–44.9) | 1.35× | 2.9× |
| range_iterate | 100000 | 35.8 (35.7–37.1) | 35.9 (33.2–37.1) | 0.99× | 2.3× |
| exception_latency | 50000 | 884.8 (883.4–955.7) | 51.2 (48.7–53.0) | 17.29× | 43.0× |
| **Geomean** | | | | **1.92×** | **7.6×** |

## Actors

- **Parallel speedup** (12 CPU-bound actors): 226.8 ms with one worker, 93.3 ms with the default pool: **2.43×**.
- **Cooperative yield:** 1000 actors, each parked on a `wait`, on 2 worker threads: 193.7 ms.
- **Message throughput:** 2000 round-trip sends (send + `wait`): 66.4 ms, about 30,108 round trips per second including start-up.

Worker scaling of `saturation_big.st` (32 actors × 50 messages of CPU work):

| Workers | ms | Speedup vs 1 |
|---:|---:|---:|
| 1 | 948.0 (910.0–988.3) | 1.00× |
| 2 | 497.2 (484.5–531.9) | 1.91× |
| 3 | 386.5 (375.6–394.1) | 2.45× |
| 4 | 293.6 (289.3–339.6) | 3.23× |
| 5 | 275.7 (270.6–285.5) | 3.44× |
| 6 | 329.2 (255.8–357.6) | 2.88× |

