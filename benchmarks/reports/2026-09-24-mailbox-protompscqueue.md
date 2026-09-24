# Actor mailbox: ProtoList + setAttributeIfEqual vs ProtoMPSCQueue

**Date:** 2026-09-24. **Machine:** DEV12 (AMD Lucienne, 6 physical cores, 62 GiB).
**Track:** protoScala roadmap Track S, mailbox half.

Both tables below were produced by `benchmarks/actors/run_mailbox_bench.sh`,
back to back, in that order, from two binaries built from the same tree: the
BEFORE binary from commit `1691a63` (the benchmark harness, with the mailbox
still an immutable `ProtoList` republished under `__mailbox__` by
`setAttributeIfEqual`), the AFTER binary from the migration to
`ProtoMPSCQueue`. Five runs per case, median reported, and every run's work
verified by the benchmark itself (the sink's own processed count against the
expected count) and its exit status checked by the runner.

**Contention.** The two runs are one minute apart with a load average of 9.01
falling to 6.65 across the first and 5.75 rising to 6.18 across the second, so
they are comparable to each other. They are not quiet-machine numbers: two
sibling agents were working in protoClojure and protoScala throughout. The
runner saw no competing proto* *benchmark* before, during or after either run,
which is what it can check; it cannot subtract a neighbour's compiler. An
earlier AFTER-only run taken at load 16.7 gave 856 ns/send where this one gives
788, so the load difference is worth roughly 8% on the enqueue figures -- small
against the differences below, but the reason the two tables were re-taken as a
pair rather than compared across an hour.

## Summary

| Measure | Before | After | Change |
|---|---|---|---|
| Send, one producer, no drain running | 1256 ns | 788 ns | **-37%** |
| Send, 8 producers, `PROTOST_WORKERS=6` | 1268 ns | 684 ns | **-46%** |
| Send, 8 producers, `PROTOST_WORKERS=1` | 63 616 ns | 3451 ns | **-95%** (18x) |
| Drain a 2000-message backlog | 1.585 s | 4.1 ms | **388x faster** |
| 200 000-message backlog | killed by the OOM reaper | 510 ms enqueue, 1.75 s drain | newly possible |

The drain column is the larger result and it is not a constant factor. The old
mailbox popped its head with `getAt(0)` + `getSlice(1, n)`, rebuilding the whole
immutable list on every message, so an undrained backlog of n messages cost
O(n^2) in time and in cells: 2000 messages drained in 1.6 s at 1.6 GB resident,
5000 in 12 s at 10.7 GB, and 10 000 was killed by earlyoom before finishing.
`takeAll` takes the whole chain in one exchange, so the same work is linear.

The send column is the smaller result and deserves a caveat: protoST's send
path does much more than enqueue -- it builds an immutable three-attribute
message envelope, a `ProtoList` of arguments and a fresh pending `Future`, then
calls `schedule()`. The compare-and-swap retry loop this migration removes was
only part of it, so a 37-46% fall is what removing that part looks like, not the
17x the queue itself shows in protoCore's own microbenchmark (PMQ-SPEC: 5717 ns
against 332 ns at 8 producers). The remaining cost is envelope construction.

`PROTOST_WORKERS=1` is the outlier worth reading: with one worker the producers
and the sink share a single thread, and the old mailbox's quadratic pop made
every context switch pay for the whole backlog again. That is where 18x comes
from, and it is why the w=1 column of the old table (63.6 us per send) is so far
from its own w=2 column (1.76 us).

## Before

# protoST mailbox send-cost benchmark

**BEFORE Track S (commit 1691a63) — mailbox is an immutable ProtoList updated with setAttributeIfEqual**

- binary: `/home/gamarino/Documentos/proyectos/protoST/build_before/protost`
- runs per case: 5 (median reported)
- load before: ` 10:17:58 up 9 days, 22:22,  1 user,  load average: 9,01, 7,37, 7,23`

## Uncontended enqueue (worker pool stopped, one producer)

| Case | Sends | Enqueue median (us) | ns / send | Drain median (us) |
|---|---|---|---|---|
| single producer | 2000 | 2513 | 1256 | 1584910 |

## Contended enqueue (8 producer actors into one sink)

| PROTOST_WORKERS | Sends | Enqueue median (us) | ns / send | Full drain median (us) |
|---|---|---|---|---|
| 1 | 2000 | 127233 | 63616 | 143591 |
| 2 | 2000 | 3516 | 1758 | 1580271 |
| 4 | 2000 | 2592 | 1296 | 1606206 |
| 6 | 2000 | 2535 | 1268 | 1568205 |

- load after: ` 10:18:36 up 9 days, 22:22,  1 user,  load average: 6,65, 6,95, 7,10`
- contended: no competing proto* benchmark was seen before, during or after the run.

## After

# protoST mailbox send-cost benchmark

**AFTER Track S — mailbox is a protoCore ProtoMPSCQueue**

- binary: `/home/gamarino/Documentos/proyectos/protoST/build_release/protost`
- runs per case: 5 (median reported)
- load before: ` 10:18:52 up 9 days, 22:23,  1 user,  load average: 5,75, 6,72, 7,02`

## Uncontended enqueue (worker pool stopped, one producer)

| Case | Sends | Enqueue median (us) | ns / send | Drain median (us) |
|---|---|---|---|---|
| single producer | 2000 | 1575 | 788 | 4081 |
| single producer, 200k backlog | 200000 | 510082 | 2550 | 1753803 |

## Contended enqueue (8 producer actors into one sink)

| PROTOST_WORKERS | Sends | Enqueue median (us) | ns / send | Full drain median (us) |
|---|---|---|---|---|
| 1 | 2000 | 6902 | 3451 | 7655 |
| 2 | 2000 | 2949 | 1474 | 6275 |
| 4 | 2000 | 1584 | 792 | 8199 |
| 6 | 2000 | 1369 | 684 | 7257 |

- load after: ` 10:19:06 up 9 days, 22:23,  1 user,  load average: 6,18, 6,78, 7,03`
- contended: no competing proto* benchmark was seen before, during or after the run.
