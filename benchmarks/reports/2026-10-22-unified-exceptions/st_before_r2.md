# protoST benchmarks — 2026-10-22

- **Host:** AMD Ryzen 5 5500U with Radeon Graphics, 12 logical CPUs, Linux 7.0.0-38-generic
- **Load average (1 min):** 5.85 before, 8.64 after
- **protoST:** protoST 0.6.6 at commit `b8b71b3`
- **CPython:** Python 3.14.0
- **Method:** 2 warmup + 5 timed runs per measurement; median wall-clock, min–max in parentheses. Every run verified its result (`VERIFIED` / `BENCH_RESULT`); a failed run fails the harness.

## Start-up

A process that evaluates one expression: protoST 38.8 ms (36.2–44.5), CPython 43.4 ms (41.7–50.8). The comparable workloads below use small N, so start-up is part of every time.

## Comparable workloads

Same algorithm, same N and same verified result as the protoPython twin. `Ratio` is protoST ÷ CPython on whole-process time (>1 means protoST is slower). At these N the process start-up is a large share of every time, so `Work ratio` also compares the times with each runtime's measured start-up subtracted: the cost of the work itself.

| Benchmark | N | protoST ms | CPython ms | Ratio | Work ratio |
|---|---:|---:|---:|---:|---:|
| int_sum_loop | 100000 | 64.6 (61.1–67.8) | 48.8 (48.0–71.2) | 1.32× | 4.8× |
| fib | 25 | 117.0 (111.2–124.9) | 63.0 (54.8–67.4) | 1.86× | 4.0× |
| list_append | 10000 | 66.0 (62.7–89.0) | 45.3 (43.1–56.8) | 1.46× | 14.4× |
| str_concat | 2000 | 38.7 (37.1–43.1) | 43.4 (40.8–45.4) | 0.89× | — |
| attr_lookup | 100000 | 73.3 (66.1–77.3) | 53.7 (52.5–86.0) | 1.37× | 3.3× |
| range_iterate | 100000 | 56.0 (50.8–58.1) | 50.3 (47.1–65.5) | 1.11× | 2.5× |
| exception_latency | 50000 | 1225.8 (1149.0–1401.8) | 69.1 (67.2–81.1) | 17.74× | 46.1× |
| **Geomean** | | | | **1.89×** | **6.9×** |

## Actors

- **Parallel speedup** (12 CPU-bound actors): 298.8 ms with one worker, 140.8 ms with the default pool: **2.12×**.
- **Cooperative yield:** 1000 actors, each parked on a `wait`, on 2 worker threads: 254.3 ms.
- **Message throughput:** 2000 round-trip sends (send + `wait`): 101.9 ms, about 19,634 round trips per second including start-up.

Worker scaling of `saturation_big.st` (32 actors × 50 messages of CPU work):

| Workers | ms | Speedup vs 1 |
|---:|---:|---:|
| 1 | 1219.8 (1187.9–1474.8) | 1.00× |
| 2 | 705.3 (665.0–747.3) | 1.73× |
| 3 | 660.5 (623.5–745.4) | 1.85× |
| 4 | 644.9 (443.0–700.4) | 1.89× |
| 5 | 456.7 (395.6–569.8) | 2.67× |
| 6 | 541.7 (434.5–611.4) | 2.25× |

