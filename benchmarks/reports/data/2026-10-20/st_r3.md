# protoST benchmarks — 2026-10-20

- **Host:** AMD Ryzen 5 5500U with Radeon Graphics, 12 logical CPUs, Linux 7.0.0-38-generic
- **Load average (1 min):** 6.37 before, 5.35 after
- **protoST:** protoST 0.6.6 at commit `3d19449`
- **CPython:** Python 3.14.0
- **Method:** 2 warmup + 5 timed runs per measurement; median wall-clock, min–max in parentheses. Every run verified its result (`VERIFIED` / `BENCH_RESULT`); a failed run fails the harness.

## Start-up

A process that evaluates one expression: protoST 26.4 ms (24.5–27.0), CPython 28.8 ms (28.6–30.9). The comparable workloads below use small N, so start-up is part of every time.

## Comparable workloads

Same algorithm, same N and same verified result as the protoPython twin. `Ratio` is protoST ÷ CPython on whole-process time (>1 means protoST is slower). At these N the process start-up is a large share of every time, so `Work ratio` also compares the times with each runtime's measured start-up subtracted: the cost of the work itself.

| Benchmark | N | protoST ms | CPython ms | Ratio | Work ratio |
|---|---:|---:|---:|---:|---:|
| int_sum_loop | 100000 | 47.6 (43.5–55.1) | 33.3 (33.0–33.8) | 1.43× | 4.7× |
| fib | 25 | 90.4 (87.0–91.0) | 41.9 (40.7–42.5) | 2.16× | 4.9× |
| list_append | 10000 | 47.1 (41.6–47.5) | 32.0 (29.0–32.7) | 1.47× | 6.5× |
| str_concat | 2000 | 28.1 (27.5–31.1) | 29.7 (29.1–30.7) | 0.95× | 2.0× |
| attr_lookup | 100000 | 54.9 (54.0–56.5) | 42.9 (40.2–44.0) | 1.28× | 2.0× |
| range_iterate | 100000 | 35.3 (34.4–36.3) | 34.4 (32.9–35.3) | 1.03× | 1.6× |
| exception_latency | 50000 | 863.5 (825.7–881.9) | 48.5 (47.1–49.4) | 17.82× | 42.5× |
| **Geomean** | | | | **1.93×** | **4.5×** |

## Actors

- **Parallel speedup** (12 CPU-bound actors): 226.7 ms with one worker, 85.2 ms with the default pool: **2.66×**.
- **Cooperative yield:** 1000 actors, each parked on a `wait`, on 2 worker threads: 182.1 ms.
- **Message throughput:** 2000 round-trip sends (send + `wait`): 63.9 ms, about 31,308 round trips per second including start-up.

Worker scaling of `saturation_big.st` (32 actors × 50 messages of CPU work):

| Workers | ms | Speedup vs 1 |
|---:|---:|---:|
| 1 | 944.5 (913.6–954.7) | 1.00× |
| 2 | 505.3 (490.4–534.8) | 1.87× |
| 3 | 368.1 (352.4–392.7) | 2.57× |
| 4 | 299.4 (281.7–320.5) | 3.15× |
| 5 | 267.3 (263.1–327.4) | 3.53× |
| 6 | 289.1 (247.4–420.9) | 3.27× |

