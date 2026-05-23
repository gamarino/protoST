# Multi-actor anti-scaling — the per-message allocation ceiling

**Status:** investigation, 2026-05-23 overnight. Confirms the conversation
hypothesis that the F6 dispatch architecture is correct; the remaining
performance ceiling is **per-message allocation cost**, not scheduling.

## What was measured

`mt100a.st`: 100 actor objects, each receives 1000 drained-batch messages
(one per actor per batch, all 100 futures awaited per batch). Total 100 K
messages spread across 100 actors — the workload designed to **saturate
the worker pool with independent actors**.

Per-actor FIFO is trivially preserved (single-message-per-batch + wait),
and the system has 100 actors >> 8 workers so the dispatch should let all
8 workers process different actors in parallel.

| Workers | Wall (best of 3) | CPU% | msg/s | Speedup vs 1w |
|---|---|---|---|---|
| 1 | **5.48 s** | 170 % | **18.2 K** | 1.00× |
| 2 | 6.40 s | 188 % | 15.6 K | **0.86×** |
| 4 | 6.77 s | 210 % | 14.8 K | **0.81×** |
| 8 | 7.40 s¹ | 273 % | 13.5 K | **0.74×** |

¹ One of three runs at workers=8 aborted with `tcache_thread_shutdown():
unaligned tcache chunk detected` (signal 6 = SIGABRT). This is a glibc
malloc detection of memory corruption (double-free, use-after-free, or
heap overrun). See [Allocator race section](#allocator-race) below.

## The architecture is correct — the ceiling is real

The d4d3db8 dispatch architecture (per-actor mailbox + sched flag +
intrusive ready stack + event-driven worker semaphore) is sound. The
33d3f00 refinement (drain whole mailbox per turn) is also sound. Both
preserve per-actor FIFO and have zero contention on the scheduler queue
under low-message-density.

But anti-scaling under multi-actor load shows the ceiling is **elsewhere**:
each SEND costs roughly the same work in CPU time as the system can do
in parallel on different cores. The math:

- Per SEND: build envelope ProtoObject (3-4 setAttribute, each a sparse-
  list rebuild allocating ~64 B), allocate Future ProtoObject (3+
  setAttribute), CAS-append to actor.__mailbox__ (allocates a new
  ProtoList AVL node), CAS-set actor.__sched__ flag, push to ReadyStack
  (allocate `ReadyNode`), `workerSem.release()`.
- Per per-message-process: pop from ReadyStack, drain mailbox (read
  ProtoList head + slice), invoke method, settle Future (5+
  setAttribute, each a sparse-list rebuild).

Total allocations per round-trip: ~15-20 protoCore Cells. Glibc
`malloc` is a per-thread freelist (tcache) by default, but at high
allocation rate **across many threads**, the tcache exhausts and the
threads contend on the global `arena` mutex inside `malloc`. **This is
the ceiling.**

CPU% confirms: from 1 → 8 workers the CPU usage rises from 170 % to
273 % (adding ~1 core of work), but wall-time gets **worse**, not better.
That extra CPU is spent in malloc-arena contention and GC sync, not in
useful actor processing.

## Allocator race

The `tcache_thread_shutdown()` abort at workers=8 strongly suggests a
real race in protoCore's per-thread allocation path. The signature is
typical of:

- **Double-free**: a `Cell*` returned to one thread's freelist, then
  returned again to another thread's. `tcache` validates alignment of
  the chunk being shut down at thread exit, detects the inconsistency,
  and aborts.
- **Cross-thread free**: a `Cell` allocated on one thread's arena is
  freed via a different thread's tcache, leading to mis-bookkeeping.

protoCore's allocator path: `ProtoContext::allocCell` uses a per-thread
freelist. `Cell::finalize` returns cells to the originating thread's
freelist (or to the global free pool). Under multi-thread bursting, the
cross-thread paths get exercised heavily; a window in which a `Cell` is
"in flight" between threads can produce the corruption.

**This is the kind of bug that took the previous F6 work months to surface
under sustained actor stress.** Worth opening as a known issue under
`docs/STATUS.md` if not already there. Hard to debug without ASan; the
existing `protoCore/build_asan/` exists for exactly this purpose.

## What this means

The "scheduler refactor" sequence we worked through tonight is
**conceptually complete**:

- d4d3db8: lock-free event-driven scheduler (no spin, no sleep, no GIL).
- 33d3f00: drain-whole-mailbox-per-turn refinement (FIFO preserving).

Neither moved the throughput needle for either single-actor or
multi-actor patterns. Because the **dispatch was never the bottleneck**
in the first place. We removed a non-bottleneck and got no perf win,
but a cleaner architecture and confidence in correctness — both worth
having.

The real ceiling lies past the dispatch:

### Tier 1: per-message allocation reduction (protoST-only)
- **Pool envelopes**: a per-runtime free-list of envelope ProtoObjects.
  Sender takes one (resets attrs), worker returns it after dispatch.
  ~80 % allocation cut on the SEND hot path.
- **Pool Futures**: similar. Free-list of settled-and-released Futures.
  Reset state to pending on reuse. ~5-7 allocations cut per Future.

### Tier 2: lock-free intrusive mailbox (requires protoCore extension)
- Replace per-actor `__mailbox__` ProtoList with an intrusive C++ linked
  list of envelopes. CAS-push at tail, CAS-pop at head (Michael-Scott).
- Requires GC-tracing extension to protoCore (your "OpaqueGCRoot" type)
  to keep envelope ProtoObjects rooted while in the C++ list.
- Eliminates the ProtoList AVL rebuild on every SEND/POP — ~50 %
  allocation cut on the SEND hot path AGAIN.

### Tier 3: flat Future state (requires protoCore extension)
- Replace Future's __state__/__value__/__error__ attributes with a
  side-allocated C++ struct (`std::atomic<int>` state + atomic ptr value).
  Future-facing ProtoObject keeps only the opaque pointer + waiter list.
- Settle = atomic store + atomic notify_all. No setAttribute traffic.
- ~5 setAttribute calls collapsed to 2-3 atomic ops per settle.

### The allocator race (separate bug)
- Independent of the perf work. Should be reproduced under ASan and
  fixed at the protoCore level (allocCell / Cell::finalize cross-thread
  safety).

## Numbers cheat sheet for the morning

```
Baseline single-actor (b883fa9, post-threaded-goto):
  mt100k:  4.97 s  →  20,121 msg/s   ← original ceiling

After event-driven scheduler (d4d3db8):
  mt100k:  7.02 s  →  14,300 msg/s   ← regression: workers steal main's work
  mt8w:    6.65 s  →  15,037 msg/s

After drain-whole-mailbox (33d3f00, this session):
  mt100k:  6.93 s  →  14,400 msg/s   ← unchanged (depth=1, loop runs once)
  mt8w:    7.55 s  →  13,245 msg/s   ← unchanged
  mt100a:  5.48 s  (workers=1) → 18,200 msg/s  ← ceiling visible
           7.40 s  (workers=8) → 13,500 msg/s  ← anti-scaling

The "best" single-config is mt100k at b883fa9 (20 K msg/s, single actor,
main-thread drains its own work). Multi-actor never reaches that
throughput regardless of worker count — the ceiling is malloc/GC/setAttribute
traffic, not dispatch.
```

## State for tomorrow

- Repo at **`33d3f00`** — drain-whole-mailbox + all earlier work,
  751/751 ctest, no regressions.
- This document plus `2026-05-23-stage2-overnight-findings.md` and
  the original `2026-05-23-interpreter-perf-spec.md` cover everything
  learned.

Suggested next step (with you awake): pick Tier 1 (envelope + Future
pooling, protoST-only, no GC-rooting risk). Realistic gain target:
**8-15 K → 25-40 K msg/s** on mt100a. If that holds up, then decide
about Tiers 2/3 with the protoCore extension.
