# protoST benchmarks — 2026-10-22

- **Host:** AMD Ryzen 5 5500U with Radeon Graphics, 12 logical CPUs, Linux 7.0.0-38-generic
- **Load average (1 min):** 7.31 before, 5.85 after
- **protoST:** protoST 0.6.6 at commit `b8b71b3`
- **CPython:** Python 3.14.0
- **Method:** 2 warmup + 5 timed runs per measurement; median wall-clock, min–max in parentheses. Every run verified its result (`VERIFIED` / `BENCH_RESULT`); a failed run fails the harness.

## Start-up

A process that evaluates one expression: protoST 33.4 ms (32.3–36.5), CPython 44.3 ms (41.4–50.1). The comparable workloads below use small N, so start-up is part of every time.

## Comparable workloads

Same algorithm, same N and same verified result as the protoPython twin. `Ratio` is protoST ÷ CPython on whole-process time (>1 means protoST is slower). At these N the process start-up is a large share of every time, so `Work ratio` also compares the times with each runtime's measured start-up subtracted: the cost of the work itself.

| Benchmark | N | protoST ms | CPython ms | Ratio | Work ratio |
|---|---:|---:|---:|---:|---:|
| int_sum_loop | 100000 | 65.2 (60.6–92.1) | 56.3 (50.5–62.1) | 1.16× | 2.7× |
| fib | 25 | 116.3 (113.7–119.9) | 60.5 (56.1–69.1) | 1.92× | 5.1× |
| list_append | 10000 | 67.2 (65.6–71.7) | 45.2 (43.2–49.9) | 1.49× | 38.7× |
| str_concat | 2000 | 43.3 (40.4–53.3) | 45.6 (43.3–54.9) | 0.95× | 7.5× |
| attr_lookup | 100000 | 74.5 (71.4–80.3) | 63.7 (57.3–67.2) | 1.17× | 2.1× |
| range_iterate | 100000 | 54.2 (49.4–72.2) | 53.1 (49.2–68.6) | 1.02× | 2.4× |
| exception_latency | 50000 | 1240.4 (1199.1–1482.7) | 69.9 (65.3–80.9) | 17.74× | 47.1× |
| **Geomean** | | | | **1.82×** | **7.1×** |

## Actors

- **Parallel speedup** (12 CPU-bound actors): 306.0 ms with one worker, 146.0 ms with the default pool: **2.10×**.
- **Cooperative yield:** 1000 actors, each parked on a `wait`, on 2 worker threads: 252.8 ms.
- **Message throughput:** 2000 round-trip sends (send + `wait`): 97.8 ms, about 20,442 round trips per second including start-up.

Worker scaling of `saturation_big.st` (32 actors × 50 messages of CPU work):

| Workers | ms | Speedup vs 1 |
|---:|---:|---:|
| 1 | 1166.7 (1160.6–1212.3) | 1.00× |
| 2 | 679.7 (654.5–698.3) | 1.72× |
| 3 | 521.5 (511.9–530.9) | 2.24× |
| 4 | 429.1 (413.8–530.9) | 2.72× |
| 5 | 478.5 (412.5–536.5) | 2.44× |
| 6 | 445.5 (405.6–556.6) | 2.62× |

