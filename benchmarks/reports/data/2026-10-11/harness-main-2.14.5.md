# protoST benchmarks — 2026-10-11

- **Host:** AMD Ryzen 5 5500U with Radeon Graphics, 12 logical CPUs, Linux 7.0.0-34-generic
- **Load average (1 min):** 3.13 before, 4.58 after
- **protoST:** protoST 0.5.0 at commit `e6ed6d0`
- **CPython:** Python 3.14.0
- **Method:** 1 warmup + 3 timed runs per measurement; median wall-clock, min–max in parentheses. Every run verified its result (`VERIFIED` / `BENCH_RESULT`); a failed run fails the harness.

## Start-up

A process that evaluates one expression: protoST 31.6 ms (30.7–33.2), CPython 36.0 ms (33.3–36.4). The comparable workloads below use small N, so start-up is part of every time.

## Comparable workloads

Same algorithm, same N and same verified result as the protoPython twin. `Ratio` is protoST ÷ CPython on whole-process time (>1 means protoST is slower). At these N the process start-up is a large share of every time, so `Work ratio` also compares the times with each runtime's measured start-up subtracted: the cost of the work itself.

| Benchmark | N | protoST ms | CPython ms | Ratio | Work ratio |
|---|---:|---:|---:|---:|---:|
| int_sum_loop | 100000 | 58.6 (57.7–63.5) | 42.0 (41.7–43.1) | 1.40× | 4.6× |
| fib | 25 | 501.0 (466.2–502.2) | 51.2 (48.6–52.3) | 9.79× | 31.0× |
| list_append | 10000 | 69.6 (62.8–72.2) | 35.2 (34.8–44.5) | 1.98× | — |
| str_concat | 2000 | 35.6 (35.3–35.9) | 34.5 (34.2–38.7) | 1.03× | — |
| attr_lookup | 100000 | 204.7 (195.1–207.6) | 46.8 (44.9–46.8) | 4.37× | 16.0× |
| range_iterate | 100000 | 95.8 (95.1–100.2) | 46.2 (42.5–60.1) | 2.07× | 6.3× |
| exception_latency | 50000 | 1381.2 (1365.1–1436.0) | 52.4 (51.1–55.2) | 26.36× | 82.4× |
| **Geomean** | | | | **3.52×** | **16.4×** |

## Actors

- **Parallel speedup** (12 CPU-bound actors): 1103.0 ms with one worker, 398.3 ms with the default pool: **2.77×**.
- **Cooperative yield:** 1000 actors, each parked on a `wait`, on 2 worker threads: 899.4 ms.
- **Message throughput:** 2000 round-trip sends (send + `wait`): 124.2 ms, about 16,105 round trips per second including start-up.

Worker scaling of `saturation_big.st` (32 actors × 50 messages of CPU work):

| Workers | ms | Speedup vs 1 |
|---:|---:|---:|
| 1 | 4113.0 (4020.6–4404.2) | 1.00× |
| 2 | 2495.2 (2466.4–2571.5) | 1.65× |
| 3 | 2172.0 (2108.0–2321.3) | 1.89× |
| 4 | 2061.5 (1966.1–2126.7) | 2.00× |
| 5 | 2078.1 (1934.2–2107.4) | 1.98× |
| 6 | 2036.3 (1999.4–2093.9) | 2.02× |

