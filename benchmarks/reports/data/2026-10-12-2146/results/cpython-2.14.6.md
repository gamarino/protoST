# protoST benchmarks — 2026-10-12

- **Host:** AMD Ryzen 5 5500U with Radeon Graphics, 12 logical CPUs, Linux 7.0.0-38-generic
- **Load average (1 min):** 4.33 before, 3.80 after
- **protoST:** protoST 0.6.4 at commit `8e59b08`
- **CPython:** Python 3.14.0
- **Method:** 1 warmup + 7 timed runs per measurement; median wall-clock, min–max in parentheses. Every run verified its result (`VERIFIED` / `BENCH_RESULT`); a failed run fails the harness.

## Start-up

A process that evaluates one expression: protoST 26.4 ms (24.7–41.1), CPython 31.9 ms (30.5–34.1). The comparable workloads below use small N, so start-up is part of every time.

## Comparable workloads

Same algorithm, same N and same verified result as the protoPython twin. `Ratio` is protoST ÷ CPython on whole-process time (>1 means protoST is slower). At these N the process start-up is a large share of every time, so `Work ratio` also compares the times with each runtime's measured start-up subtracted: the cost of the work itself.

| Benchmark | N | protoST ms | CPython ms | Ratio | Work ratio |
|---|---:|---:|---:|---:|---:|
| int_sum_loop | 100000 | 52.2 (49.6–56.2) | 39.1 (35.4–41.7) | 1.34× | 3.6× |
| fib | 25 | 396.2 (378.2–404.0) | 44.5 (43.0–45.8) | 8.90× | 29.2× |
| list_append | 10000 | 53.2 (47.1–64.5) | 33.2 (31.8–34.9) | 1.61× | 21.2× |
| str_concat | 2000 | 29.9 (28.6–30.4) | 32.5 (30.4–35.6) | 0.92× | 5.4× |
| attr_lookup | 100000 | 185.0 (179.3–204.1) | 45.3 (43.5–47.2) | 4.09× | 11.8× |
| range_iterate | 100000 | 82.6 (81.0–90.4) | 36.8 (35.9–40.9) | 2.24× | 11.5× |
| exception_latency | 50000 | 1100.1 (1027.5–1159.5) | 51.5 (49.2–57.0) | 21.37× | 54.8× |
| **Geomean** | | | | **3.20×** | **13.7×** |

