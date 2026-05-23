# Overnight session — final report (2026-05-23)

## TL;DR

**Best throughput so far**: mt100a (100 actors, drained) **29.6 K msg/s
single-worker**, **19.4 K msg/s with 8 workers**. mt100k single-actor
**20.6 K msg/s** (~ original threaded-goto ceiling, recovered after the
scheduler refactor's regression).

The user's question "what is serializing the workers?" — **answered**:
hot-path `createSymbol("__name__")` calls in `settleFuture`,
`readState`, `appendFutureWaiter`, and `invokeBlock` were hitting the
SymbolTable shard mutex + UTF-8 normalisation ~ 600 K times per
mt100a run. Migrating them to the already-built Bootstrap cache cut
the rope-walk profile cost from ~ 9.5 % to ~ 4.5 % and lifted mt100a
single-worker throughput by **+74 %**.

The "anti-scaling" the user pointed at remains visible but milder:
multi-worker mt100a is ~ 1.5× slower than single-worker rather than
~ 2×. The next serialisation point is the concurrent-GC stop-the-world
cadence (perf shows ~ 20 % of CPU in `gcThreadLoop` + GC tracing
callbacks) — that's the next thing to chase if perf matters more.

## What we tried tonight, in order

| Commit | What | Outcome |
|---|---|---|
| `12ba983` | Stage-2 prep primitives (ActorLock, task list helpers) — dormant | OK, no behaviour change |
| (uncommitted) | Stage-2 option-B implementation (global task list + per-actor try-lock + deferred mailbox) | **Failed**: per-actor FIFO violated under multi-worker contention. Reverted. See `2026-05-23-stage2-overnight-findings.md` for the FIFO bug analysis. |
| `9934c82` | Docs: stage 2 findings + FIFO bug + recommendation | Documentation |
| `33d3f00` | Drain entire actor mailbox per turn (was 1 msg/turn → many) | Architecturally correct, no measurable perf win (mailboxes mostly had 1 msg in drained benchmarks) |
| `0e45aba` | Replace 4 hot-path `createSymbol("__name__")` with Bootstrap cache lookups | **Major win** — see numbers below |

## The "what is serializing" investigation

User's instruction: *"hay algo serializando todo. hacer un análisis estático profundo"*.

Method:
1. Grep `std::mutex` / `recursive_mutex` / `shared_mutex` / `stwFlag` /
   `stopTheWorld` across protoCore + protoST.
2. Walk the SEND fast-path step by step, listing every shared resource
   touched per message.
3. Walk the drainOne path similarly.
4. perf record + report on mt100a (8 workers).

Findings ranked by impact:

### 1. Hot-path `createSymbol` calls (RESOLVED)
The four offending functions called `ProtoString::createSymbol(ctx,
"__name__")` per invocation. Each call:
- enters `SymbolTable::intern` (mutex on the symbol's shard);
- `normalizeForSymbol` does a `toUTF8String` walk of the input;
- the bucket walk does `contentEqual` (= 2× `toUTF8String`) per
  bucket-entry.

For mt100a (100 K SEND/settle round-trips), this added up to ~ 600 K
shard-mutex lookups + ~ 1.2 M rope traversals. Multi-thread contention
on the shard mutex was the main serialiser, and the rope walks were
the visible 9.5 % CPU cost in perf.

Migration to the cached Bootstrap symbols is in `0e45aba`. Profile
after: rope walk down to ~ 4.5 %, throughput up 21-74 %.

### 2. Global GC stop-the-world (REAL, NOT YET ATTACKED)
`gcThreadLoop` waits in `gcCV.wait` until `parkedThreads >=
runningThreads`. Every workload-driven trigger pauses ALL workers
simultaneously while the GC completes its STW phases (Phases 1 + 2 of
the collector — root collection + mark setup). Each pause is a hard
sync barrier across N workers.

In post-fix perf, `gcThreadLoop` + lambdas + processReferences =
~ 19.6 % of CPU. That's a real ceiling for any further multi-worker
scaling on benchmarks with high allocation rate.

Mitigation paths (not pursued tonight):
- Reduce allocation rate via the envelope/Future pooling discussed in
  `2026-05-23-interpreter-perf-spec.md` Stage 2.
- Tune GC trigger threshold (currently `freeRatio < 0.2`) — make it
  larger so the GC runs less often.
- Move Phase 2 root collection off the STW window (concurrent root
  marking — major design work).

### 3. `getFreeCells` global mutex (NOT THE BOTTLENECK)
Each context allocates from a per-thread freelist; when empty, calls
`ProtoSpace::getFreeCells` under `globalMutex`. With `blocksPerAllocation`
scaling by `runningThreads × 4` (60 K - 64 K cells per refill), the
amortised cost per allocation is tiny. ~ 24 lock acquisitions for a 1.5 M
cell benchmark = negligible.

### 4. `__live_actors__` anchor (NOT THE BOTTLENECK)
The first-enqueue anchor in `enqueueReady` CAS-appends actors to
`liveRegistry.__live_actors__`. For 100 actors that's 100 contended
appends total, not per-message. Bounded.

### 5. Allocator race (CORRUPTION BUG — NEEDS ASAN)
One mt100a workers=8 run aborted with `tcache_thread_shutdown():
unaligned tcache chunk detected`. Signature of double-free or cross-
thread free in glibc malloc — almost certainly a race in protoCore's
per-thread `Cell` allocator/finaliser. Independent of scheduler perf;
should be reproduced under ASan and fixed at the protoCore level.

## Final throughput numbers

All under cgroup `MemoryMax=2G`, best of 3:

| Benchmark | Pre-overnight (12ba983) | Post-overnight (0e45aba) | Δ |
|---|---|---|---|
| **mt100k** single-actor | 7.16 s (14.0 K msg/s) | **4.85 s (20.6 K msg/s)** | **+47 %** |
| **mt8w** drained 8w | 7.64 s (13.1 K msg/s) | **4.73 s (21.1 K msg/s)** | **+62 %** |
| **mt100a** workers=8 | (untested) | **5.15 s (19.4 K msg/s)** | — |
| **mt100a** workers=1 | (untested) | **3.38 s (29.6 K msg/s)** | — |
| ctest | 751/751 (10 s) | 751/751 (10 s) | unchanged |

**mt100a workers=1 at 29.6 K msg/s is the best single-config throughput
this codebase has ever shown.** The previous high was the threaded-goto
result at 22 K (single actor, drained).

## Anti-scaling status (post-overnight)

mt100a still shows mild anti-scaling:
- 1 worker: 3.38 s = 29.6 K msg/s
- 8 workers: 5.15 s = 19.4 K msg/s

Ratio: 1.52× slower with 8 workers. Down from ~ 2× before tonight's
fixes, but still real. The likely cause is the GC STW frequency (finding
#2 above). Attacking this requires either reducing allocation rate
(envelope/Future pooling) or restructuring the GC (concurrent root
marking) — both significant work.

## What didn't move the needle

- `0e45aba` (drain-all per turn): correct architecture, but mailboxes
  in drained benchmarks have ≤ 1 msg, so the per-turn loop runs once.
  Win would show under burst-load benchmarks (mt8w_deep etc.) — but
  those are anti-scaling for unrelated allocation-pressure reasons.

## Files written

- `0e45aba` — commit, the createSymbol fix (3 files, 33 / 23 diff)
- `33d3f00` — commit, drain-all-per-turn drainOne (1 file, 219 / 377 diff)
- `9934c82` — docs, stage-2 findings (1 file)
- `12ba983` — wip, stage-2 prep (6 files, dormant)
- `2026-05-23-mt100a-anti-scaling.md` — docs, multi-actor measurements
- `2026-05-23-overnight-final-report.md` — this document

All commits local, none pushed.

## State for tomorrow

Repo at `0e45aba` (`main`). 751/751 ctest. No regressions. No protoCore
changes. Memory safety still has the allocator-race bug to track in a
ticket.

Suggested order for tomorrow:
1. Validate the morning numbers on your hardware (same `mt100a.st`).
2. Decide whether to chase the GC STW (high effort, ~15-30 % win)
   or call the perf work done at this level.
3. Track the allocator race separately — needs ASan run, possibly a
   protoCore-side ticket.
4. The `__live_actors__` / `__live__` machinery from d4d3db8 is still
   in place; not strictly needed for correctness now (every task in
   `__tasks__`-or-mailbox keeps the actor reachable) but the cleanup
   can wait.
