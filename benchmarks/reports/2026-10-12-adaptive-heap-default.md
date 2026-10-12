# The adaptive heap controller as protoST's default — 2026-10-12

**Synthetic benchmarks, one notebook-class machine** (DEV12: AMD Ryzen 5
5500U, 6 cores / 12 threads, 62 GB, Ubuntu 26.04, kernel 7.0.0, GCC 15.2).
Both builds are Release builds made by the same compiler against the same
protoCore 2.14.5 library (`ldd` recorded in the JSON):

- **0.6.1**: the 0.6.1 release sources: fixed heap limit of 10M cells
  (640 MB), engine slot arrays pre-sized to 1 M slots;
- **0.6.2**: the adaptive heap controller by default (`d213945`; budget here
  75 % of 62 GB, soft limit starting at 128 MB) **and** engine slot arrays
  sized on demand (`K6` fix, commit "K6: size each engine context's slot
  array on demand").

Wall time, CPU time (`ru_utime + ru_stime` from `wait4`, all threads) and
peak RSS are medians of 7 interleaved runs after one discarded round, from
`benchmarks/compare_builds.py` with each build listed twice (A/A pairs for the
noise floor). Every run verified its computed result. Cycles and
instructions are `perf stat -r 5 -e cycles,instructions` (all threads, so the
collector's work is included). Collector pauses come from an instrumented
build of the same protoCore 2.14.5 (`PROTOCORE_GC_PROFILE=1`). No compiler,
test runner or other runtime was running (the harness records it per run);
a desktop session (browser, editor) was open, and the 1-minute load was
1.8-2.4 for the single-threaded rows and 3.2-4.2 for the actor rows (the
multi-worker `saturation_big` rows load the machine themselves, up to 8.9).

Raw data: [`data/2026-10-12/`](data/2026-10-12/): `compare-061-vs-062.json`
(every sample), `perfstat-061-vs-062.json` and `perfstat.py`,
`gc-phases.txt`, `demo2.txt`, `alloc-loop-3M.txt`; the files suffixed
`-before-k6` are the same measurements before the slot-array fix.

## Answer

With both changes, **0.6.2 is faster than 0.6.1 on every workload but one,
which is unchanged within the noise (`attr_lookup`), and uses a fifth to a
half of the memory on every workload that allocates.** The actor workloads
gain the most: `parallel_speedup` with the full pool -27 %, on one worker
-28 %, `message_throughput` -41 %, `saturation_big` -16 % (one worker) to
-43 % (six). The talk's demo 2 shows a median speedup of 4.45x (4.26-4.98)
instead of 4.3x (3.98-4.76). The cost is collector CPU on the recursion- and
exception-heavy single-threaded programs (`fib` +18 % CPU,
`exception_latency` +8 %), which run faster in wall time all the same, and
peak memory on an allocation-bound loop is no longer capped (see below).

### The regression the first measurement found, and its fix (K6)

Measured first with the controller alone (`*-before-k6` files),
`parallel_speedup` with the full pool ran **14 % slower** than 0.6.1 and
demo 2 dropped to about 3.6x. Cause: protoCore's stop-the-world root scan
visits every slot of every thread's `automaticLocals`, and protoST pre-sized
that array to 1,048,576 slots (8 MB) for every thread that had ever run code,
so each pause scanned about 13 M slots with twelve workers. Under the fixed
limit these short benchmarks never collected, which hid the cost; under the
controller they collect several times. `perf record` of that run put the
root scan (`gcThreadLoop`'s `scanContexts` lambda and `isCellPointer`) at
7.7 % of all samples, more than the sweep (4.6 %).

The fix (protoST only, no protoCore change) sizes the array on demand: it
starts at 4,096 slots and doubles when a frame region needs more, up to the
same maximum, so the recursion depth limit is unchanged; the transient-pin
scratch region moved from the top of the array to its bottom. Growing
reallocates the array, which is safe because only the owning thread grows it,
while running, and the collector reads a context's array only during a
stop-the-world phase, with the owner parked (details in
`src/runtime/TransientPin.h`). Test: `EngineSlots` in
`tests/unit/test_heap_policy.cpp`.

Stop-the-world pauses (`gc-phases.txt`, one run each):

| Workload | 0.6.1 cycles | pauses, total / max | 0.6.2 before K6 fix: cycles, total / max | 0.6.2: cycles | pauses, total / max |
|---|---:|---|---|---:|---|
| `attr_lookup` | 0 | — | 1, 4.2 / 4.2 ms | 1 | 0.11 / 0.11 ms |
| `fib` | 0 | — | 3, 13.9 / 5.1 ms | 3 | 0.22 / 0.10 ms |
| `exception_latency` | 0 | — | 5, 22.6 / 4.8 ms | 5 | 0.36 / 0.10 ms |
| `parallel_speedup` w=1 | 0 | — | 5, 32.9 / 7.0 ms | 5 | 0.25 / 0.06 ms |
| `parallel_speedup` pool | 0 | — | 7, 175 / 31 ms | 6 | 1.1 / 0.27 ms |
| `cooperative_yield` w=2 | 0 | — | 5, 41.4 / 8.6 ms | 5 | 0.29 / 0.07 ms |
| `saturation_big` w=1 | 5 | 35.4 / 8.1 ms | 33, 236 / 9.6 ms | 30 | 2.2 / 0.11 ms |
| `saturation_big` w=6 | 6 | 113 / 19.9 ms | 27, 547 / 31 ms | 29 | 3.9 / 0.27 ms |

The longest pause anywhere is now 0.27 ms, against 8-20 ms under 0.6.1's
fixed limit whenever it did collect.

### Wall, CPU and memory (medians of 7)

| Workload | 0.6.1 wall ms | 0.6.2 wall ms | Δ wall | A/A (0.6.1, 0.6.2) | 0.6.1 CPU ms | 0.6.2 CPU ms | Δ CPU | 0.6.1 RSS | 0.6.2 RSS |
|---|---:|---:|---:|---|---:|---:|---:|---:|---:|
| startup | 29 | 24 | -16.2 % | +1.4 / -1.6 % | 31 | 27 | -13.9 % | 35 MB | 27 MB |
| int_sum_loop | 52 | 48 | -7.8 % | -1.3 / -3.9 % | 54 | 51 | -6.6 % | 35 MB | 27 MB |
| fib | 388 | 365 | -6.0 % | +2.2 / -1.2 % | 418 | 492 | +17.6 % | 306 MB | 148 MB |
| list_append | 53 | 48 | -8.2 % | +2.7 / -0.5 % | 81 | 80 | -1.2 % | 67 MB | 59 MB |
| str_concat | 32 | 29 | -10.4 % | +4.5 / -2.6 % | 35 | 31 | -10.1 % | 35 MB | 27 MB |
| attr_lookup | 187 | 176 | -5.7 % | -9.3 / -3.8 % | 222 | 205 | -8.0 % | 146 MB | 139 MB |
| range_iterate | 86 | 83 | -3.5 % | +5.4 / +2.2 % | 116 | 114 | -2.0 % | 86 MB | 79 MB |
| exception_latency | 1,091 | 965 | -11.5 % | +1.2 / -1.0 % | 1,116 | 1,203 | +7.8 % | 515 MB | 150 MB |
| parallel_speedup pool | 342 | 293 | -14.6 % | +17.7 / -15.7 % | 1,486 | 1,251 | -15.8 % | 654 MB | 152 MB |
| parallel_speedup w=1 | 960 | 661 | -31.2 % | -0.7 / +4.3 % | 961 | 743 | -22.7 % | 583 MB | 139 MB |
| cooperative_yield w=2 | 745 | 522 | -30.0 % | -2.0 / -1.0 % | 1,002 | 857 | -14.5 % | 515 MB | 145 MB |
| message_throughput | 132 | 70 | -47.3 % | -9.9 / +7.4 % | 283 | 211 | -25.2 % | 126 MB | 55 MB |
| saturation_big w=1 | 3,429 | 2,858 | -16.7 % | -4.1 / +0.2 % | 3,912 | 3,613 | -7.6 % | 638 MB | 139 MB |
| saturation_big w=2 | 2,202 | 1,715 | -22.1 % | +1.0 / -3.5 % | 4,505 | 3,930 | -12.8 % | 648 MB | 144 MB |
| saturation_big w=3 | 1,780 | 1,267 | -28.8 % | -0.0 / -2.8 % | 5,096 | 4,247 | -16.7 % | 657 MB | 152 MB |
| saturation_big w=4 | 1,712 | 1,098 | -35.9 % | -2.3 / +2.6 % | 6,211 | 4,821 | -22.4 % | 667 MB | 148 MB |
| saturation_big w=5 | 1,741 | 1,049 | -39.8 % | -1.5 / +4.6 % | 7,506 | 5,340 | -28.9 % | 677 MB | 173 MB |
| saturation_big w=6 | 1,797 | 1,016 | -43.5 % | -0.2 / +1.2 % | 8,975 | 5,965 | -33.5 % | 687 MB | 209 MB |

Three rows are noisy and are better read from all 14 runs of each version
(both copies pooled): `attr_lookup` 171 -> 173 ms (+1.0 %, unchanged),
`parallel_speedup` pool 352 -> 256 ms (-27 %; every 0.6.2 run, 210-338 ms,
was below the 0.6.1 median), `message_throughput` 121 -> 72 ms (-41 %).

Why the short single-threaded programs (start-up, `int_sum_loop`,
`str_concat`, `list_append`) also got 8-16 % faster: they never collect, so
the policy does not matter to them; they no longer allocate and fill an
8 MB slot array per thread that runs code (13 % fewer instructions at
start-up, 8 MB less RSS).

### Cycles and instructions (`perf stat -r 5`, all threads)

| Workload | 0.6.1 Mcycles | 0.6.2 Mcycles | Δ cycles | 0.6.1 Minstr | 0.6.2 Minstr | Δ instr |
|---|---:|---:|---:|---:|---:|---:|
| startup | 118 | 96 | -18.0 % | 105 | 92 | -12.9 % |
| int_sum_loop | 192 | 174 | -9.0 % | 300 | 287 | -4.4 % |
| fib | 1,483 | 1,727 | +16.5 % | 2,231 | 2,420 | +8.5 % |
| list_append | 269 | 255 | -5.5 % | 274 | 260 | -5.0 % |
| str_concat | 129 | 110 | -14.6 % | 133 | 120 | -9.5 % |
| attr_lookup | 652 | 664 | +1.8 % | 967 | 1,026 | +6.0 % |
| range_iterate | 369 | 344 | -6.7 % | 452 | 439 | -2.8 % |
| exception_latency | 3,867 | 4,353 | +12.6 % | 6,554 | 6,648 | +1.4 % |
| parallel_speedup pool | 3,993 | 3,372 | -15.5 % | 5,900 | 5,675 | -3.8 % |
| parallel_speedup w=1 | 3,068 | 2,439 | -20.5 % | 5,776 | 5,638 | -2.4 % |
| cooperative_yield w=2 | 3,239 | 2,911 | -10.1 % | 4,460 | 4,417 | -1.0 % |
| message_throughput | 930 | 739 | -20.5 % | 417 | 291 | -30.2 % |
| saturation_big w=1 | 12,279 | 11,834 | -3.6 % | 28,020 | 27,337 | -2.4 % |
| saturation_big w=2 | 13,586 | 11,476 | -15.5 % | 27,947 | 26,976 | -3.5 % |
| saturation_big w=3 | 15,136 | 11,841 | -21.8 % | 28,802 | 27,113 | -5.9 % |
| saturation_big w=4 | 17,119 | 12,485 | -27.1 % | 29,310 | 27,162 | -7.3 % |
| saturation_big w=5 | 19,817 | 13,293 | -32.9 % | 29,806 | 27,152 | -8.9 % |
| saturation_big w=6 | 22,888 | 14,835 | -35.2 % | 30,437 | 27,176 | -10.7 % |

Where cycles and instructions rise (`fib`, `exception_latency`,
`attr_lookup`), it is the collector: 0.6.1 never collected on those runs.
The multi-worker rows lose cycles to page faults and contention for fresh
memory under the fixed limit, which lets the heap grow to 650-690 MB without
collecting.

### Demo 2 of the talk (`demo2.txt`, 10 alternating runs)

`02-actors-parallelism.st`, as `run_demo.sh` ran it for 0.6.1 (with its
20M-cell limit) and as it runs it for 0.6.2 (default policy):

| | one thread (median) | 12 actors (median) | speedup, median (range) |
|---|---:|---:|---|
| 0.6.1 | 1,753 ms | 413 ms | 4.30x (3.98-4.76) |
| 0.6.2 | 1,662 ms | 373 ms | 4.45x (4.26-4.98) |

### An allocating loop (the `cli_memory_bounded` program, default settings)

`s := 0. 1 to: 3000000 do: [:i | s := s + (Array new: 10) size]`, three
alternating runs each (`alloc-loop-3M.txt`):

| | wall | user | peak RSS |
|---|---|---|---|
| 0.6.1 | 29.0-30.9 s | 53-57 s | 677 MB (all three) |
| 0.6.2 | 20.1-25.0 s | 30-45 s | **0.88 GB, 2.7 GB, 3.5 GB** |

Faster, but **the peak is no longer bounded by 640 MB**: the controller
doubles its soft limit while each doubling reduces the time the program
waits for the collector, up to its budget (here 46 GB). The largest peaks
came with the fastest runs. (Before the K6 fix the same loop peaked at
0.19-1.86 GB.) A program that must stay within a fixed amount of memory
should set `PROTOCORE_HEAP_LIMIT_CELLS`; a cgroup or job-object limit, when
there is one, also lowers the budget.

### Very large collections (K5)

`(1 to: 500000) asArray`: 0.6.1 stops with the out-of-memory message (exit
3); 0.6.2 succeeds in 1.7 s, 0.74 GB peak. `(1 to: 1000000) asArray`: 0.6.2
succeeds in 4.3 s, 1.7 GB peak. (Measured before the K6 fix, which does not
affect them.)

## Tests

Release build, `ctest -j4`: 1100/1100 (the 1094 of 0.6.1, five `HeapPolicy`
cases and `EngineSlots`); the default-policy case and `EngineSlots` failed
before their changes. The four talk demos pass 20/20 each. Three existing
tests needed attention under the controller; none was a runtime defect:

- the IvarGroups atomicity test hung on its first run: its C++ reader thread
  spins without allocating and never reached a safepoint, so the first
  stop-the-world could not start while the writer was parked for it (both
  stacks captured live with gdb). The reader now calls
  `ProtoContext::safepoint()`;
- the S15 test failed once and took 60 s once on the Linux CI runner
  ("best cycle reclaimed 0"); it sampled `reclaimedLastCycle` only while it
  waited, so a cycle that ran during a program was never counted. It now
  observes every cycle and prints them all on failure. 150 local runs (GCC
  15 and, in a container, GCC 13) never failed, so whether this was the whole
  cause is not proven;
- `cli_buried_actor_deadlock` failed once on the macOS CI runner with #(7):
  its main program paused a fixed 100 ms and assumed the single worker had
  started A's run by then; when it had not, the LIFO ready queue ran D's ping
  first and A correctly answered it (a pause of 0 gives #(7) every time). It
  now waits for a flag A sets.
