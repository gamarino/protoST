# protoST benchmarks — 2026-10-11

- **Host:** AMD Ryzen 5 5500U with Radeon Graphics, 12 logical CPUs, Linux 7.0.0-34-generic
- **Load average (1 min):** 4.58 before, 4.48 after
- **protoST:** protoST 0.5.0 at commit `e6ed6d0`
- **CPython:** Python 3.14.0
- **Method:** 1 warmup + 3 timed runs per measurement; median wall-clock, min–max in parentheses. Every run verified its result (`VERIFIED` / `BENCH_RESULT`); a failed run fails the harness.

## Start-up

A process that evaluates one expression: protoST 38.1 ms (35.9–38.9), CPython 41.4 ms (41.1–42.7). The comparable workloads below use small N, so start-up is part of every time.

## Comparable workloads

Same algorithm, same N and same verified result as the protoPython twin. `Ratio` is protoST ÷ CPython on whole-process time (>1 means protoST is slower). At these N the process start-up is a large share of every time, so `Work ratio` also compares the times with each runtime's measured start-up subtracted: the cost of the work itself.

| Benchmark | N | protoST ms | CPython ms | Ratio | Work ratio |
|---|---:|---:|---:|---:|---:|
| int_sum_loop | 100000 | 65.8 (65.0–67.8) | 42.7 (42.0–47.4) | 1.54× | 22.5× |
| fib | 25 | 537.8 (500.2–540.7) | 51.5 (51.0–51.9) | 10.44× | 49.7× |
| list_append | 10000 | 77.4 (73.1–83.5) | 44.2 (39.4–58.0) | 1.75× | 14.4× |
| str_concat | 2000 | 37.7 (36.4–42.0) | 37.2 (35.9–38.2) | 1.01× | — |
| attr_lookup | 100000 | 217.4 (214.4–228.8) | 54.4 (51.0–58.8) | 4.00× | 13.9× |
| range_iterate | 100000 | 111.8 (107.6–117.5) | 40.6 (39.8–42.2) | 2.76× | — |
| exception_latency | 50000 | 1491.0 (1392.1–1611.8) | 66.2 (62.3–67.0) | 22.54× | 58.8× |
| **Geomean** | | | | **3.55×** | **26.5×** |

