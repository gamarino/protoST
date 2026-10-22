# protoST benchmarks — 2026-10-22

- **Host:** AMD Ryzen 5 5500U with Radeon Graphics, 12 logical CPUs, Linux 7.0.0-38-generic
- **Load average (1 min):** 7.02 before, 7.31 after
- **protoST:** protoST 0.6.6 at commit `b8b71b3`
- **CPython:** Python 3.14.0
- **Method:** 2 warmup + 5 timed runs per measurement; median wall-clock, min–max in parentheses. Every run verified its result (`VERIFIED` / `BENCH_RESULT`); a failed run fails the harness.

## Start-up

A process that evaluates one expression: protoST 36.0 ms (33.4–38.3), CPython 48.9 ms (43.1–53.6). The comparable workloads below use small N, so start-up is part of every time.

## Comparable workloads

Same algorithm, same N and same verified result as the protoPython twin. `Ratio` is protoST ÷ CPython on whole-process time (>1 means protoST is slower). At these N the process start-up is a large share of every time, so `Work ratio` also compares the times with each runtime's measured start-up subtracted: the cost of the work itself.

| Benchmark | N | protoST ms | CPython ms | Ratio | Work ratio |
|---|---:|---:|---:|---:|---:|
| int_sum_loop | 100000 | 67.3 (64.7–69.7) | 52.5 (50.3–69.5) | 1.28× | 8.7× |
| fib | 25 | 116.1 (112.0–135.3) | 58.1 (55.4–64.4) | 2.00× | 8.6× |
| list_append | 10000 | 67.9 (63.6–71.8) | 75.4 (66.1–101.1) | 0.90× | 1.2× |
| str_concat | 2000 | 49.3 (37.6–69.4) | 52.5 (51.7–55.0) | 0.94× | 3.7× |
| attr_lookup | 100000 | 75.1 (69.7–81.5) | 66.6 (56.3–80.0) | 1.13× | 2.2× |
| range_iterate | 100000 | 57.0 (50.9–74.5) | 49.2 (45.5–66.8) | 1.16× | — |
| exception_latency | 50000 | 1210.0 (1180.3–1233.9) | 74.1 (67.4–76.3) | 16.32× | 46.5× |
| **Geomean** | | | | **1.73×** | **5.7×** |

## Actors

- **Parallel speedup** (12 CPU-bound actors): 308.8 ms with one worker, 133.3 ms with the default pool: **2.32×**.
- **Cooperative yield:** 1000 actors, each parked on a `wait`, on 2 worker threads: 325.8 ms.
- **Message throughput:** 2000 round-trip sends (send + `wait`): 139.0 ms, about 14,385 round trips per second including start-up.

Worker scaling of `saturation_big.st` (32 actors × 50 messages of CPU work):

| Workers | ms | Speedup vs 1 |
|---:|---:|---:|
| 1 | 1133.3 (1105.7–1234.0) | 1.00× |
| 2 | 663.3 (650.1–688.2) | 1.71× |
| 3 | 500.2 (491.8–502.4) | 2.27× |
| 4 | 479.4 (407.2–508.9) | 2.36× |
| 5 | 407.2 (353.1–460.1) | 2.78× |
| 6 | 411.5 (346.6–463.1) | 2.75× |

