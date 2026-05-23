# The worker-scaling plateau was the hardware, not the runtime (2026-05-23)

## TL;DR

After landing every dispatch-path optimisation in the session
(className/classSide cache, newFuture trim, envelope immutable,
relaxed memory ordering, stale-wakeup fix), saturation_big *still*
regressed from w=4 to w=8. Two final experiments closed the
investigation:

1. **Three NACTORS variants** (`saturation_8a.st`, `saturation_32a.st`,
   `saturation_128a.st`) — same total work, NACTORS ∈ {8, 32, 128}.
   All three plateaued at w=8 IDENTICALLY. The plateau is INDEPENDENT
   of per-actor contention (8a has 1 actor per worker, no
   work-stealing, no mailbox sharing — and still regresses).

2. **Worker count sweep w∈{1,2,4,6,8,12}** on saturation_32a.

Result:

| workers | wall  | speedup | notes                            |
|---|---|---|---|
| 1  | 7.87 s | 1.00×    | baseline                         |
| 2  | 4.64 s | 1.70×    | linear-ish                       |
| 4  | 2.78 s | 2.83×    | near-ideal 4×                    |
| 6  | **2.53 s** | **3.11×** | **OPTIMAL — = N physical cores**  |
| 8  | 3.10 s | 2.54×    | REGRESSION (SMT contention)       |
| 12 | 3.14 s | 2.51×    | all SMT siblings saturated        |

The "plateau" everyone was hunting was AMD Ryzen 5 5500U's
**6 physical cores × 2 SMT threads = 12 logical**. w=1..6 ride the
physical cores in isolation; w=7..12 force SMT siblings into the
same core, where contention for shared L1/L2 and execution units
swamps the marginal extra parallelism. The 1.2-1.4× per-SMT-pair
ratio typical of CPU-bound workloads cannot beat the lost
single-thread perf from the contention; net negative.

## Why the runtime fixes didn't break the SMT ceiling

The runtime improvements landed in the session DID work — they
just couldn't push past a hardware limit that lives below them.
Throughput end-to-end:

| benchmark | session start | session end | improvement |
|---|---|---|---|
| mt100k w=1 | ~ 20.6 K msg/s | 36.6 K | +78 % |
| mt100a w=1 | ~ 29.6 K | 65 K | +120 % |
| mt100a w=2 (best) | — | **67.6 K** | new ceiling |
| saturation_big w=6 (best) | n/a (regressed) | 3.11× scaling | from regression |

The pre-session runs were ALSO bounded by 6 cores; the workloads
just spent enough of their wall in serialisable bottlenecks
(symbol-table mutex, futex storms on workerSem, etc.) that the
hardware ceiling was invisible behind the software one.

## Two distinct ceilings now, both physical

### CPU-bound workload — saturation_big

Ceiling = N physical cores. Above that, SMT cannot help because
the work is allocation-heavy + branch-heavy and saturates the
core's execution units already. Sweet spot is **w = number of
physical cores** (6 on this machine).

### Producer-bound workload — mt100a

mt100a has a single producer (main) doing fan-out + fan-in per
batch. main's SEND fast-path is the throughput limiter; the
worker side is mostly idle waiting on the next batch. Sweet
spot is **w = 2**: one worker absorbs main's stream at line rate,
a second one buffers the tail; more workers add contention on
the ready queue / actor lock for no extra parallelism. Beyond
w=2 throughput DEGRADES (49 K at w=8 vs 67.6 K at w=2).

To get mt100a higher than 67 K msg/s the producer side has to
speed up (less work per SEND) OR there has to be more than one
producer thread. The yieldable-do: limitation (see
`2026-05-23-multiproducer-blocker.md`) is what blocks the
driver-actor-side fan-out that would let workers double as
producers.

## What the stale-wakeup fix actually moved (independent of cores)

The `finishDrain` schedState==2 spurious re-enqueue (commit
`f21aab4`) was a real bug — it halved the drain count from 2×NACTORS
to NACTORS and removed a one-worker-park-storm pattern that was
distorting earlier perf numbers. Even if the throughput improvement
under SMT-saturated load is hard to see vs the hardware ceiling,
the bug fix is correct on its own merits and keeps the worker-
stats output clean (parks now [2,2,...,2] uniformly).

## Operational guidance

- For benchmarking CPU-bound workloads, use `PROTOST_WORKERS=N`
  where N is the number of physical cores. Above it, SMT
  contention REGRESSES the result. The machine's physical core
  count is the value reported by
  `lscpu | grep "Núcleo(s) por «socket»"` × number of sockets.
- For benchmarking producer-bounded workloads (any benchmark
  where main does the SENDs in a loop), the sweet spot is
  often w=1 or w=2. Test both.
- The `PROTOST_WORKER_STATS=1` env var prints per-worker drain
  + park counters on shutdown. Use it to confirm that all
  workers are getting equal work; a single worker with
  disproportionate parks usually points at a re-enqueue or
  wake-up bug.
- The three saturation_Na.st variants (`8a`, `32a`, `128a`) are
  preserved as a regression set for the NACTORS-vs-NWORKERS
  question. Any future "scaling broke at w=X" hypothesis can
  be tested against them: if all three regress at the same
  point, the cause is hardware; if only one does, it points
  at per-actor contention.

## Session-end commits

protoST (local, not pushed):
- `f21aab4` — fix(scheduler): finishDrain skips spurious re-enqueue
- `5523bf8` — perf(send): newFuture down to 1 setAttribute, envelope immutable
- `ea35db9` — perf(send): cache __class_name__/__class_side__ on Bootstrap
- ... earlier commits (multi_producer, coalesce-snapshot fix, GC trigger removal docs, etc.)

protoCore (local, not pushed):
- `90aade34` — perf(mutable): relax shard-root reads to relaxed
- `ed38a499` — perf(cell): relax memory_order on internalSetNextRaw
- `ea2c17f4` — gc: do not trigger from getFreeCells when no heap cap

751/751 ctest. Nothing pushed.
