# The adaptive heap controller as protoST's default — 2026-10-12

**Synthetic benchmarks, one notebook-class machine** (DEV12: AMD Ryzen 5
5500U, 6 cores / 12 threads, 62 GB, Ubuntu 26.04, kernel 7.0.0, GCC 15.2).
Both builds are Release builds made by the same compiler against the same
protoCore 2.14.5 library (`ldd` recorded in the JSON):

- **0.6.1**: the 0.6.1 release sources, fixed heap limit of 10M cells (640 MB);
- **0.6.2**: the same sources plus commit `d213945`, which makes
  `STRuntime::configureHeap` call `ProtoSpace::enableAdaptiveHeap()`
  (budget here: 75 % of 62 GB; soft limit starting at 128 MB).

Wall time, CPU time (`ru_utime + ru_stime` from `wait4`, all threads) and
peak RSS are medians of 7 interleaved runs after one discarded round, from
`benchmarks/compare_builds.py` with each build listed twice (A/A pairs for the
noise floor). Every run verified its computed result. Cycles and
instructions are `perf stat -r 5 -e cycles,instructions` (all threads, so the
collector's work is included). The 1-minute load stayed at or below 3.3 for
the single-threaded rows and reached 6.6 during the six-worker rows; no
compiler, test runner or other runtime was running (the harness records it per
run). A desktop session (browser, editor) was open.

Raw data: [`data/2026-10-12/`](data/2026-10-12/) — `compare-061-vs-062.json`
(every sample), `perfstat-061-vs-062.json` and `perfstat.py`,
`gc-phases.txt` (collector phase totals per run), `alloc-loop-3M.txt`.

## Answer

Nine rows differ beyond the A/A noise: the controller is faster on seven
of them (6-26 % in wall time) and slower on two, and it uses a third to a
quarter of the memory on every workload that allocates. **It is slower in two cases that matter:**

- **`parallel_speedup` with the full pool (12 workers): +14 % wall** (335 ->
  382 ms; A/A +2.6 / -1.5 %), and the talk's demo 2 drops from about 4.2x
  to about 3.6x (12 actors: 0.6.1 424-446 ms, 0.6.2 468-514 ms, one run
  760 ms; the one-thread half is unchanged).
- **`attr_lookup`: +7.4 % wall, +10 % cycles** — one collection that the
  fixed limit never ran.

Single-threaded programs that allocate little (`int_sum_loop`, `str_concat`,
`list_append`, `range_iterate`, `message_throughput`, start-up) do not change.
`fib` is faster in wall time (`exception_latency` within the noise), but both spend 18 % more CPU:
the collector now runs (3 and 5 cycles) where the fixed limit ran none.

### Root cause of the regression: stop-the-world cost per thread

`gc-phases.txt` (an instrumented build of protoCore 2.14.5,
`PROTOCORE_GC_PROFILE=1`) shows that **under the fixed limit none of these
workloads collects at all** except `saturation_big` (5-6 cycles): they finish
before the heap reaches 640 MB. Under the controller they collect 1-33 times,
and each cycle's stop-the-world pause is long:

| Workload | 0.6.1 cycles | 0.6.2 cycles | 0.6.2 pause per cycle (mean / max) | threads parked, summed |
|---|---:|---:|---|---:|
| `attr_lookup` | 0 | 1 | 4.2 / 4.2 ms | 0 ms |
| `fib` | 0 | 3 | 4.6 / 5.1 ms | 14 ms |
| `parallel_speedup` w=1 | 0 | 5 | 6.6 / 7.0 ms | 33 ms |
| `parallel_speedup` pool | 0 | 7 | 25 / 31 ms | 1,168 ms |
| `saturation_big` w=1 | 5 | 33 | 7.1 / 9.6 ms | 239 ms |
| `saturation_big` w=6 | 6 | 27 | 20 / 31 ms | 3,257 ms |

The pause grows with the number of threads. `perf record` of the
`parallel_speedup` pool run attributes it to protoCore's root scan of each
thread's context: on the collector thread, `gcThreadLoop`'s `scanContexts`
lambda and `isCellPointer` take 7.7 % of all the run's samples, the sweep
4.6 %. protoST pre-sizes every engine context's `automaticLocals` to
`ExecutionEngine::kSlotCapacity` = 1,048,576 slots (8 MB) and keeps its
frames there; the root scan visits every slot of every thread's array,
used or not: 13 contexts x 1 M slots per pause in the pool run. The cost was
there before 0.6.2, but under the fixed limit the short benchmarks never
paid it. **This is protoST's, not the controller's**, and it is not fixed
in this release; see *Open*.

### Wall, CPU and memory (medians of 7)

| Workload | 0.6.1 wall ms | 0.6.2 wall ms | Δ wall | A/A (0.6.1, 0.6.2) | 0.6.1 CPU ms | 0.6.2 CPU ms | Δ CPU | 0.6.1 RSS | 0.6.2 RSS |
|---|---:|---:|---:|---|---:|---:|---:|---:|---:|
| startup | 30 | 30 | -0.2 % | +5.1 / +0.4 % | 33 | 33 | +0.0 % | 33 MB | 33 MB |
| int_sum_loop | 52 | 54 | +3.9 % | +4.0 / +1.9 % | 55 | 57 | +3.4 % | 33 MB | 34 MB |
| fib | 413 | 383 | -7.4 % | +1.5 / +5.3 % | 442 | 523 | +18.4 % | 305 MB | 156 MB |
| list_append | 56 | 56 | +0.1 % | +5.9 / -1.1 % | 87 | 88 | +1.3 % | 65 MB | 65 MB |
| str_concat | 33 | 34 | +3.2 % | +0.2 / -1.3 % | 36 | 37 | +3.4 % | 34 MB | 34 MB |
| attr_lookup | 175 | 188 | **+7.4 %** | -4.4 / +0.1 % | 210 | 221 | +5.2 % | 146 MB | 146 MB |
| range_iterate | 88 | 85 | -3.9 % | -0.5 / +1.1 % | 119 | 120 | +1.1 % | 85 MB | 85 MB |
| exception_latency | 1,075 | 1,049 | -2.5 % | -0.5 / -7.1 % | 1,096 | 1,287 | +17.5 % | 514 MB | 157 MB |
| parallel_speedup pool | 335 | 382 | **+14.0 %** | +2.6 / -1.5 % | 1,534 | 1,405 | -8.5 % | 654 MB | 227 MB |
| parallel_speedup w=1 | 942 | 696 | -26.1 % | -0.4 / +2.3 % | 942 | 780 | -17.2 % | 582 MB | 154 MB |
| cooperative_yield w=2 | 749 | 585 | -21.9 % | -4.7 / -3.2 % | 999 | 929 | -7.0 % | 514 MB | 168 MB |
| message_throughput | 121 | 118 | -2.9 % | -0.2 / +0.1 % | 261 | 263 | +0.8 % | 121 MB | 122 MB |
| saturation_big w=1 | 3,215 | 3,019 | -6.1 % | +1.4 / +1.2 % | 3,675 | 3,727 | +1.4 % | 637 MB | 154 MB |
| saturation_big w=2 | 2,078 | 1,903 | -8.4 % | +3.9 / -1.3 % | 4,246 | 4,018 | -5.4 % | 647 MB | 167 MB |
| saturation_big w=3 | 1,745 | 1,698 | -2.7 % | +1.9 / +0.7 % | 5,052 | 4,793 | -5.1 % | 656 MB | 179 MB |
| saturation_big w=4 | 1,619 | 1,676 | +3.5 % | +2.1 / -1.6 % | 5,965 | 5,291 | -11.3 % | 667 MB | 183 MB |
| saturation_big w=5 | 1,663 | 1,467 | -11.8 % | +0.8 / +0.2 % | 7,232 | 5,523 | -23.6 % | 673 MB | 231 MB |
| saturation_big w=6 | 1,751 | 1,441 | -17.7 % | +1.6 / +7.3 % | 8,787 | 6,245 | -28.9 % | 688 MB | 244 MB |

Within the noise: start-up, `int_sum_loop`, `list_append`, `str_concat`,
`range_iterate`, `message_throughput`, `saturation_big` w=3 and w=4 (the A/A
pairs differ by up to 4-6 % in this session). The 0.6.1 wins on the actor
rows come from page faults, not from collecting: the fixed limit lets the
heap grow to 510-690 MB of fresh pages and never collects them.

### Cycles and instructions (`perf stat -r 5`, all threads)

| Workload | 0.6.1 Mcycles | 0.6.2 Mcycles | Δ cycles | 0.6.1 Minstr | 0.6.2 Minstr | Δ instr |
|---|---:|---:|---:|---:|---:|---:|
| startup | 120 | 123 | +2.6 % | 105 | 105 | +0.2 % |
| int_sum_loop | 194 | 195 | +0.6 % | 301 | 301 | +0.0 % |
| fib | 1,510 | 1,960 | +29.8 % | 2,232 | 2,569 | +15.1 % |
| list_append | 290 | 287 | -0.9 % | 274 | 275 | +0.3 % |
| str_concat | 130 | 130 | +0.1 % | 133 | 133 | -0.2 % |
| attr_lookup | 648 | 714 | +10.2 % | 970 | 1,076 | +11.0 % |
| range_iterate | 374 | 385 | +3.0 % | 452 | 453 | +0.0 % |
| exception_latency | 3,879 | 4,412 | +13.8 % | 6,554 | 6,824 | +4.1 % |
| parallel_speedup pool | 4,172 | 3,967 | -4.9 % | 5,904 | 6,787 | +15.0 % |
| parallel_speedup w=1 | 3,008 | 2,740 | -8.9 % | 5,773 | 5,964 | +3.3 % |
| cooperative_yield w=2 | 3,159 | 3,062 | -3.1 % | 4,464 | 4,811 | +7.8 % |
| message_throughput | 934 | 940 | +0.6 % | 423 | 421 | -0.4 % |
| saturation_big w=1 | 12,439 | 12,755 | +2.5 % | 28,022 | 29,138 | +4.0 % |
| saturation_big w=2 | 13,711 | 13,205 | -3.7 % | 27,965 | 29,698 | +6.2 % |
| saturation_big w=3 | 15,523 | 14,276 | -8.0 % | 28,893 | 31,002 | +7.3 % |
| saturation_big w=4 | 18,003 | 15,476 | -14.0 % | 29,400 | 32,293 | +9.8 % |
| saturation_big w=5 | 20,634 | 16,034 | -22.3 % | 29,731 | 31,909 | +7.3 % |
| saturation_big w=6 | 23,166 | 16,230 | -29.9 % | 30,306 | 31,335 | +3.4 % |

Instructions are identical where nothing collects (the interpreter did not
change); everywhere else the extra instructions are the collector's. Cycles
fall on the multi-worker rows although instructions rise: under the fixed
limit those runs spend cycles on page faults and, with five or six workers,
on contention for fresh memory.

### An allocating loop (the `cli_memory_bounded` program, default settings)

`s := 0. 1 to: 3000000 do: [:i | s := s + (Array new: 10) size]`, three
alternating runs each (`alloc-loop-3M.txt`):

| | wall | user | peak RSS |
|---|---|---|---|
| 0.6.1 | 30.9-33.6 s | 56-60 s | 677 MB (all three) |
| 0.6.2 | 22.1-30.3 s | 36-54 s | **0.19 GB, 0.47 GB, 1.86 GB** |

Faster, but **the peak is no longer bounded by 640 MB**: the controller
doubles its soft limit while each doubling reduces the time the program waits
for the collector, up to its budget (here 46 GB). The run that reached
1.86 GB was the fastest. A program that must stay within a fixed amount of
memory should set `PROTOCORE_HEAP_LIMIT_CELLS` (the cgroup or job-object
limit, when there is one, also lowers the budget).

### Very large collections (K5)

`(1 to: 500000) asArray`: 0.6.1 stops with the out-of-memory message (exit
3); 0.6.2 succeeds in 1.7 s, 0.74 GB peak. `(1 to: 1000000) asArray`: 0.6.2
succeeds in 4.3 s, 1.7 GB peak.

## Tests

Release build, `ctest -j4`: 1099/1099 (the 1094 of 0.6.1 plus five
`HeapPolicy` cases). The default-policy case failed before the change. One
existing test hung under the controller on the first run: the IvarGroups
atomicity test's C++ reader thread spins without allocating and never reached
a safepoint, so the first stop-the-world could not start while the writer was
parked for it (both stacks captured live with gdb). It is a defect of the
test, not of the runtime (protoST's interpreter reaches a safepoint at every
loop back-edge and call); the reader now calls `ProtoContext::safepoint()`.
The four talk demos pass 20/20 each under the controller.

## Open

- **The per-thread root scan.** Each protoST thread keeps a 1 M-slot
  `automaticLocals` array that every stop-the-world scans in full, 2-4 ms
  per thread on this machine. Two ways out, neither taken here: protoST could
  size the array on demand (it is pre-sized because the interpreter keeps raw
  pointers into it, so growing it while frames are live needs care), or
  protoCore could scan only a used prefix that the embedder declares (an
  API change, which is the maintainer's call). Until then, the more threads
  a program runs, the longer each pause.
- Peak memory under the controller depends on the run (the 3M loop above:
  0.19-1.86 GB); the policy is speed within a large budget, not a small
  footprint.
