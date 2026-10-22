# protoST benchmarks — 2026-10-22

- **Host:** AMD Ryzen 5 5500U with Radeon Graphics, 12 logical CPUs, Linux 7.0.0-38-generic
- **Load average (1 min):** 8.97 before, 10.41 after
- **protoST:** protoST 0.6.6 at commit `b8b71b3`
- **CPython:** Python 3.14.0
- **Method:** 2 warmup + 5 timed runs per measurement; median wall-clock, min–max in parentheses. Every run verified its result (`VERIFIED` / `BENCH_RESULT`); a failed run fails the harness.

## Start-up

A process that evaluates one expression: protoST 37.9 ms (33.8–38.6), CPython 46.5 ms (43.4–52.4). The comparable workloads below use small N, so start-up is part of every time.

## Comparable workloads

Same algorithm, same N and same verified result as the protoPython twin. `Ratio` is protoST ÷ CPython on whole-process time (>1 means protoST is slower). At these N the process start-up is a large share of every time, so `Work ratio` also compares the times with each runtime's measured start-up subtracted: the cost of the work itself.

| Benchmark | N | protoST ms | CPython ms | Ratio | Work ratio |
|---|---:|---:|---:|---:|---:|
| int_sum_loop | 100000 | 70.2 (64.1–75.6) | 50.5 (46.7–67.4) | 1.39× | 8.1× |
| fib | 25 | 117.8 (106.7–146.9) | 63.7 (54.7–78.7) | 1.85× | 4.7× |
| list_append | 10000 | 67.7 (66.1–91.4) | 43.2 (41.5–53.3) | 1.57× | — |
| str_concat | 2000 | 41.3 (34.5–64.9) | 57.6 (44.0–71.3) | 0.72× | 0.3× |
| attr_lookup | 100000 | 114.9 (105.8–119.8) | 66.7 (60.0–86.8) | 1.72× | 3.8× |
| range_iterate | 100000 | 51.9 (51.1–74.2) | 56.7 (54.1–64.3) | 0.92× | 1.4× |
| exception_latency | 50000 | 1280.8 (1240.3–2011.0) | 73.8 (67.8–90.5) | 17.35× | 45.5× |
| **Geomean** | | | | **1.87×** | **3.8×** |

## Actors

- **Parallel speedup** (12 CPU-bound actors): 333.6 ms with one worker, 131.9 ms with the default pool: **2.53×**.
- **Cooperative yield:** 1000 actors, each parked on a `wait`, on 2 worker threads: 257.8 ms.
- **Message throughput:** 2000 round-trip sends (send + `wait`): 117.7 ms, about 16,996 round trips per second including start-up.

Worker scaling of `saturation_big.st` (32 actors × 50 messages of CPU work):

| Workers | ms | Speedup vs 1 |
|---:|---:|---:|
| 1 | 1227.4 (1146.2–1330.8) | 1.00× |
| 2 | 698.4 (671.1–715.1) | 1.76× |
| 3 | 519.6 (506.9–656.3) | 2.36× |
| 4 | 465.6 (445.8–475.1) | 2.64× |
| 5 | 513.2 (423.0–555.7) | 2.39× |
| 6 | 474.7 (431.2–526.1) | 2.59× |

