# protoST benchmarks — 2026-10-22

- **Host:** AMD Ryzen 5 5500U with Radeon Graphics, 12 logical CPUs, Linux 7.0.0-38-generic
- **Load average (1 min):** 7.51 before, 8.97 after
- **protoST:** protoST 0.6.6 at commit `b8b71b3`
- **CPython:** Python 3.14.0
- **Method:** 2 warmup + 5 timed runs per measurement; median wall-clock, min–max in parentheses. Every run verified its result (`VERIFIED` / `BENCH_RESULT`); a failed run fails the harness.

## Start-up

A process that evaluates one expression: protoST 36.9 ms (35.2–43.3), CPython 48.5 ms (45.4–51.6). The comparable workloads below use small N, so start-up is part of every time.

## Comparable workloads

Same algorithm, same N and same verified result as the protoPython twin. `Ratio` is protoST ÷ CPython on whole-process time (>1 means protoST is slower). At these N the process start-up is a large share of every time, so `Work ratio` also compares the times with each runtime's measured start-up subtracted: the cost of the work itself.

| Benchmark | N | protoST ms | CPython ms | Ratio | Work ratio |
|---|---:|---:|---:|---:|---:|
| int_sum_loop | 100000 | 71.7 (65.5–73.3) | 61.5 (57.1–70.6) | 1.17× | 2.7× |
| fib | 25 | 114.1 (110.6–127.5) | 66.8 (60.8–69.6) | 1.71× | 4.2× |
| list_append | 10000 | 75.5 (66.6–88.8) | 52.1 (46.3–63.6) | 1.45× | 10.7× |
| str_concat | 2000 | 40.8 (37.6–42.7) | 51.5 (48.0–65.8) | 0.79× | 1.3× |
| attr_lookup | 100000 | 75.6 (66.9–86.1) | 62.3 (58.8–63.8) | 1.21× | 2.8× |
| range_iterate | 100000 | 50.3 (46.8–54.6) | 53.1 (44.3–66.2) | 0.95× | 2.9× |
| exception_latency | 50000 | 1238.2 (1205.2–1304.0) | 72.8 (68.3–74.9) | 17.00× | 49.4× |
| **Geomean** | | | | **1.72×** | **4.9×** |

## Actors

- **Parallel speedup** (12 CPU-bound actors): 272.5 ms with one worker, 128.9 ms with the default pool: **2.11×**.
- **Cooperative yield:** 1000 actors, each parked on a `wait`, on 2 worker threads: 255.6 ms.
- **Message throughput:** 2000 round-trip sends (send + `wait`): 113.9 ms, about 17,562 round trips per second including start-up.

Worker scaling of `saturation_big.st` (32 actors × 50 messages of CPU work):

| Workers | ms | Speedup vs 1 |
|---:|---:|---:|
| 1 | 1130.9 (1122.4–1244.2) | 1.00× |
| 2 | 657.4 (632.5–686.6) | 1.72× |
| 3 | 512.5 (488.1–735.2) | 2.21× |
| 4 | 425.8 (412.1–582.5) | 2.66× |
| 5 | 387.1 (359.3–487.5) | 2.92× |
| 6 | 423.8 (394.8–460.1) | 2.67× |

