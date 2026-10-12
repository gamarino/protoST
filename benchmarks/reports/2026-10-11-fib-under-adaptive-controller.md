# Why `fib.st` was three times slower under protoCore's adaptive controller — 2026-10-11

**Synthetic benchmark, one notebook-class machine** (DEV12: AMD Ryzen 5 5500U,
6 cores / 12 threads, Ubuntu 26.04, GCC 15.2). Every comparison below uses the
same compiler for every build it compares. Hardware performance counters were
not available in this session (`perf_event_paranoid` is 4 after the machine's
upgrade, and changing it needs `sudo sysctl kernel.perf_event_paranoid=1`), so
the figures are wall time and CPU time from `wait4` (`ru_utime`, `ru_stime`),
minor faults and peak RSS, medians of 9 interleaved runs, plus protoCore's own
collector trace (`PROTOCORE_HEAP_TRACE=1`) and phase profile
(`PROTOCORE_GC_PROFILE=1`, a `PROTOCORE_GC_INSTRUMENT=ON` build of 2.14.5).
Every run was started only with the one-minute load below 3 and no compiler,
test runner or other runtime running (the harness records it per run). Raw
data and scripts: [`data/2026-10-11/bisection/`](data/2026-10-11/bisection/)
(`fib.json`, `fixmeasure.json`, `perfbench.py`).

## Answer

**The cause was in protoST, not in the controller, and it was a correctness
bug as well as a performance one.** protoST hands the garbage a program makes
to protoCore's collector only at a *safepoint*: before this fix, a loop
back-edge or the entry of a new engine (S15, `ExecutionEngine::gcSafepoint`).
A method send within the same engine was neither. `fib.st` only recurses: it
has no loop and calls no block through a primitive, so after start-up it
reached no safepoint at all. Everything it allocated stayed on the root
context's *young generation*, which protoCore treats as live until it is
submitted. So:

- **Under protoST's fixed limit** (10 M cells, 640 MB) `fib 25` allocates
  less than the limit and no collection ever runs: 0 cycles. The fixed limit
  was fast only because it never collected.
- **Under the controller** the soft limit starts at 2 M cells (128 MB); the
  heap reaches it during the run and cycles start. They could not reclaim
  anything (25,369 cells freed in two cycles), and each had to walk the whole
  pinned young chain: 4.1 M young cells in the first cycle, 8.2 M in the
  second. The mutator waited for headroom during the first; the process
  waited at exit for the second.
- **And with a larger argument the program died.** `fib 29` ran out of
  memory under the default 640 MB limit ("the live objects fill the heap
  limit ... and the last collections reclaimed nothing", exit 3) on 0.5.0,
  0.6.0 and `main`; `fib 25` did so under a 3 M or 4 M cell limit. Its real
  live set is a few dozen frames.

**Fix (0.6.1):** call entry is a safepoint too. Right after a send or a block
call has pushed the callee's frame, every live value is in a traced frame
slot, which is the precondition `ProtoContext::safepoint()` documents, the
same one engine entry already relied on. The submission is threshold-gated
inside protoCore (10,000 cells per context), so a call pays a few atomic
loads. Test: `tests/cli/test_cli_gc_recursion_reclaims.sh` (`ctest -R
cli_gc_recursion_reclaims`): `fib 25` must finish under a 2.7 M cell ceiling,
and must run out of memory with `PROTOST_NO_GC_SAFEPOINT=1`. It failed before
the fix (exit 3, "reclaimed nothing") and passes after it; the whole suite
passes, 1094/1094.

## Measurements

### Before the fix: what the controller did (`main` at `f19abe4`, protoCore 2.14.5)

| Configuration | Wall ms | User ms | Sys ms | Minor faults | Peak RSS | Cycles |
|---|---:|---:|---:|---:|---:|---:|
| fixed 10 M-cell limit (protoST's default) | 409 | 237 | 200 | 76,922 | 305 MB | 0 |
| no limit at all (`PROTOCORE_HEAP_LIMIT_CELLS=0`) | 403 | 232 | 195 | 76,920 | 305 MB | 0 |
| controller (`PROTOCORE_ADAPTIVE_HEAP=1`) | 1,157 | 664 | 560 | 234,496 | 593 MB | 2 |
| controller, pacing off (`PROTOCORE_GC_PACING=0`) | 652 | 360 | 326 | 134,323 | 334 MB | 1 |
| controller, soft limit starting at the budget (`PROTOCORE_ADAPTIVE_HEAP_START=budget`) | 403 | 234 | 195 | 76,920 | 305 MB | 0 |

A/A copies of the first two rows: fixed 390 ms (-4.6 %), controller 1,169 ms
(+1.0 % against its twin); the noise floor of this session is about 2-5 %.

The collector's own account of the controller run (instrumented 2.14.5,
cumulative over the two cycles where marked "total"):

| | Cycle 1 | Cycle 2 |
|---|---:|---:|
| live set `L` reported | 1,680,423 | 3,394,024 |
| young cells walked as roots | 4,110,366 | 8,223,360 |
| time walking young chains | 189 ms | 388 ms |
| tracing from the other roots | 70 ms | 144 ms |
| mark phase (P4) | 259 ms | 531 ms |
| cycle duration `Tc` | 286 ms | 681 ms |
| cells freed | 25,369 | 1 |
| mutator time waiting for headroom | 264 ms (6 waits) | 18 ms |
| why it started | heap at the 2 M-cell soft limit right after start-up | pacing: runway 4.4 M cells > headroom; still running at exit |

(`[GC-PHASES]` lines, `young=`, `trace=`, `headroom_wait=`; with pacing off
only the first cycle runs and the run takes 0.65 s.) Each cycle also
faulted in about 230-310 MB of fresh pages (sys time 0.20 -> 0.56 s), since
the heap grew by the soft limit's doublings while nothing was freed.

### The three hypotheses

| Hypothesis | Verdict |
|---|---|
| The controller starts from a small soft limit, so a program with a tiny working set collects very often, each cycle paying a fixed cost | **Partly.** It collected twice, not often; the cost per cycle was not fixed but proportional to the *pinned young chain*, which grew with everything the program had ever allocated. The working set was not tiny from the collector's point of view: nothing had been submitted. |
| The 640 MB fixed limit simply never collects | **Confirmed.** 0 cycles; identical to running with no limit at all. |
| Pacing or early wake triggers on allocation rate | **Confirmed for the second cycle**: pacing started it early (runway 4.4 M cells against the remaining headroom), and the process waited for it at exit. Pacing off: one cycle, 0.65 s. Pacing made a futile cycle cost more; it did not make it futile. |

### After the fix (medians of 9, same compiler, same protoCore 2.14.5 build, interleaved)

| Workload | Policy | Before | After | A/A (before, after) |
|---|---|---:|---:|---|
| `fib.st` | fixed limit | 409 ms, 305 MB | 404 ms, 305 MB | 419 / 420 ms |
| `fib.st` | controller | 1,187 ms, 593 MB | **403 ms, 155 MB** | |
| `parallel_speedup`, 1 worker | fixed limit | 922 ms, 581 MB | 925 ms, 581 MB | 924 / 926 ms |
| `parallel_speedup`, 1 worker | controller | 686 ms, 153 MB | 677 ms, 153 MB | |
| `cooperative_yield`, 2 workers | fixed limit | 740 ms, 513 MB | 746 ms, 510 MB | 725 / 723 ms |
| `cooperative_yield`, 2 workers | controller | 564 ms, 167 MB | 569 ms, 168 MB | |
| `saturation_big`, 1 worker | fixed limit | 3,234 ms, 637 MB | 3,089 ms, 637 MB | 3,178 / 3,184 ms |
| `saturation_big`, 1 worker | controller | 2,801 ms, 154 MB | 2,862 ms, 154 MB | |

Under the controller, after the fix, `fib.st` runs three cycles of 52-65 ms
each, every one reclaiming (live set 0.2-0.5 M cells, against the 1.68 M the
pinned chain made it look like), and finishes in the same time as under the
fixed limit with half the memory. Under the fixed limit the extra safepoints
cost nothing measurable: `fib.st`, the most call-heavy workload, moves by
-1.1 % against an A/A spread of +2.5 / +2.8 % (CPU time +4.6 % against an A/A
spread of +6.0 / +5.4 %).

`saturation_big` ran with the machine less quiet (load up to 7 in a third of
its runs: another agent's tests); its rows move within that noise.

## What this changes

- **K5 (protoST keeps the fixed limit) loses its reason.** The decision of
  2026-10-04 rested on `fib.st` being three times slower under the controller;
  after this fix it is not, and the controller is 10-27 % faster on the three
  actor workloads above, with about a third of the memory (the fixed limit
  lets the heap grow to 510-640 MB, and faulting those pages in is most of
  their system time). Enabling the controller by default is a policy decision
  for the maintainer and is **not** made here; the measurement says it is
  worth re-running protoCore's calibration rows for protoST with this fix.
- **Nothing to change in protoCore.** The controller did what it was designed
  to do with what it was given; the futile cycles were a symptom of protoST
  not submitting its garbage. An initial soft limit above the start-up live
  set (`PROTOCORE_ADAPTIVE_HEAP_START=budget`) only hid it, by never
  collecting, as the fixed limit did.
- **Still open, not measured here:** a program whose only allocation happens
  inside a single long primitive (no send, no loop, no new engine in it)
  still reaches no safepoint until the primitive returns.
