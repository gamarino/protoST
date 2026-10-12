# protoST benchmarks — 2026-10-12

- **Host:** AMD Ryzen 5 5500U with Radeon Graphics, 12 logical CPUs, Linux 7.0.0-38-generic
- **Load average (1 min):** 3.80 before, 3.38 after
- **protoST:** protoST 0.6.4 at commit `8e59b08`
- **CPython:** Python 3.14.0
- **Method:** 1 warmup + 7 timed runs per measurement; median wall-clock, min–max in parentheses. Every run verified its result (`VERIFIED` / `BENCH_RESULT`); a failed run fails the harness.

## Start-up

A process that evaluates one expression: protoST 25.0 ms (24.2–25.7), CPython 34.8 ms (32.8–36.9). The comparable workloads below use small N, so start-up is part of every time.

## Comparable workloads

Same algorithm, same N and same verified result as the protoPython twin. `Ratio` is protoST ÷ CPython on whole-process time (>1 means protoST is slower). At these N the process start-up is a large share of every time, so `Work ratio` also compares the times with each runtime's measured start-up subtracted: the cost of the work itself.

| Benchmark | N | protoST ms | CPython ms | Ratio | Work ratio |
|---|---:|---:|---:|---:|---:|
| int_sum_loop | 100000 | 56.1 (47.7–75.6) | 39.5 (36.9–42.8) | 1.42× | 6.7× |
| fib | 25 | 412.3 (379.8–470.6) | 45.9 (44.4–50.1) | 8.98× | 35.0× |
| list_append | 10000 | 54.7 (44.7–56.8) | 34.9 (33.5–37.0) | 1.57× | — |
| str_concat | 2000 | 32.2 (28.6–33.8) | 35.5 (34.8–38.6) | 0.91× | 11.1× |
| attr_lookup | 100000 | 197.3 (188.3–202.3) | 44.1 (42.7–47.7) | 4.47× | 18.6× |
| range_iterate | 100000 | 86.4 (81.4–92.0) | 37.9 (35.6–40.4) | 2.28× | 19.9× |
| exception_latency | 50000 | 1148.5 (1108.6–1171.3) | 52.3 (50.3–54.6) | 21.96× | 64.4× |
| **Geomean** | | | | **3.28×** | **19.9×** |

