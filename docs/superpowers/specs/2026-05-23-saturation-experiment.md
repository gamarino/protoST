# Saturation experiment — isolating pool drain capacity from producer rate (2026-05-23)

## Why

Every benchmark prior to this one was measuring a probability
distribution, not the pool's maximum capacity. SEND fast-paths from
main, worker drains, and Future settles are all racing; wall time
reflects whichever side was slower at any instant. To answer "how
much CPU can the pool actually pull at given N workers?", we need to
stop the race: load the pool to saturation, then time the pure drain.

## The mechanism

A `WorkerPool` singleton exposes two primitives to Smalltalk:

```smalltalk
WorkerPool stopProcessing.   "workers park at the top of their loop"
"... pre-load mailboxes here ..."
WorkerPool startProcessing.  "release the pool"
```

C++ side (`src/runtime/STRuntime.cpp`):

- New `processingPaused` atomic flag plus a `pauseMutex` /
  `pauseCV` pair on the Impl struct.
- `workerLoop` checks the flag at the **top** of every iteration
  BEFORE `drainOne`. On a hit, the worker parks on `pauseCV`
  (bracketed by `enter/exitGcBlocking` so a paused worker still
  counts as parked for any concurrent STW GC). Common path is one
  atomic load — no measurable overhead.
- `enqueueReady` (the per-send wake) **skips** the
  `workerSem.release()` while paused. Pre-fix, a benchmark that
  enqueued 16 K messages under pause called `release()` 16 K times,
  exceeding the `std::counting_semaphore<8192>` LeastMaxValue —
  undefined behaviour, observed to hang the runtime.
- `startProcessing` flips the flag, `notify_all`s `pauseCV`, AND
  releases one `workerSem` permit per worker. The latter covers
  workers that parked on `workerSem.acquire()` BEFORE the pause was
  set (their queue was empty at the time) — without these makeup
  permits they would stay parked despite the queue now being full.

## The benchmark

`benchmarks/actors/saturation.st`:

- 16 worker actors, 10 messages per actor (160 total).
- `Worker >> work` is a 5 000-iteration captured-var sum
  (~ 20-30 ms of CPU each).
- Sequence: `stopProcessing` → load all 160 sends → snapshot t0 →
  `startProcessing` → wait every Future → snapshot t1. Print the
  drain elapsed milliseconds.
- 16 actors > 8 workers, so workers steal across actors and the
  ready-queue / actor-lock path is exercised (not just a static
  worker-per-actor assignment).

## Numbers

Best of 2 runs under `MemoryMax=12G`, machine idle, MaxRSS ~ 1.0 GiB
in every config:

| workers | drain ms | speedup vs w=1 | comment                          |
|---|---|---|---|
| 1 | 3 905   | 1.00×          | single-thread baseline           |
| 2 | 2 258   | **1.73×**      | the brutal w=1 → w=2 we expected |
| 4 | 2 048   | 1.91×          | tail of meaningful scaling       |
| 8 | 1 989   | 1.96×          | plateau                          |

### Ruling out the cgroup as the residual ceiling

Re-ran the same sweep under `MemoryMax=32G` (5× headroom over the
observed MaxRSS), best of 3:

| workers | drain ms | MaxRSS  | comment           |
|---|---|---|---|
| 1 | 3 987   | 1.01 GiB | matches 12G run  |
| 2 | 2 387   | 1.01 GiB | 1.67× (≈ 1.73× at 12G) |
| 4 | 2 373   | 1.02 GiB | 1.68×             |
| 8 | 2 201   | 1.02 GiB | 1.81×             |

The cap never engaged — MaxRSS is identical to the 12G run, the
sweep curve is identical within noise. **The plateau ~ 2× is real
and is not the cgroup talking**. protoCore's GC fires on freelist
exhaustion (not on a memory-watermark ratio), so more headroom
does not reduce its cadence; the residual ceiling is somewhere
else.

Comparison with the older shape on the SAME hardware, same cgroup
(no producer-rate isolation, drivers + main + workers all racing):

| benchmark            | w=1 → w=2 | w=1 → w=8 |
|---|---|---|
| `multi_producer.st`  | 1.53×     | 1.52×     |
| `parallel_speedup.st`| 1.55×     | 1.67×     |
| **`saturation.st`**  | **1.73×** | **1.96×** |

Two things land cleanly:

1. **The brutal w=1 → w=2 is real and reproducible**. The
   producer/consumer race that earlier measurements were averaging
   over was hiding ~ 0.2× of headroom. With the race removed, the
   speedup is 1.73× — close to the ideal 2× for trivially-parallel
   work.

2. **There is a residual plateau around 2× from w=4 onward**, which
   the producer race wasn't responsible for. The likeliest cause is
   the GC / allocator path: every `work()` invocation builds a
   fresh captured-variable dict and writes to it 5 000 times,
   roughly 800 K mutable-attribute updates total across the run.
   With 8 workers all driving that allocation rate, GC cycles fire
   more often per unit of mutator progress and the STW Phase-1
   barrier serialises the pool.

The saturation pattern doesn't itself diagnose that residual
ceiling, but it cleanly excludes the producer-race / Future-settle
race / wake-up path from the suspect list — those used to hide
the GC ceiling behind noisier ceilings.

## Operational notes

- The pause-gate API is the right surface for any future "pre-load
  → measure drain" experiment. New benchmarks should follow this
  shape.
- ALWAYS run under a cgroup cap. The captured-var allocation
  burst can outrun earlyoom under sustained pause-then-release
  loads. ≥ 8 G is comfortable for the current 160-msg config;
  scale up the cap as you scale the message count.
- The 8192-permit `std::counting_semaphore` is now safe against
  enqueue bursts during a pause (releases are skipped), but
  callers using `enqueueTask` directly are NOT guarded — if a
  future code path enqueues > 8 K tasks while paused via that
  alternative entry point, bump the semaphore's LeastMaxValue.

## What's NOT the bottleneck (measured)

### B. The captured-dict path is NOT the dominant allocation
driver

Rewrote `work()` to put `sum` into an instance variable, swapping
`STORE_CAPTURED` for `STORE_INSTVAR` (sparing the fresh-dict
allocation per `work()` invocation and exercising the
already-existent `_iv_sum` slot for the 5 000 inner writes). Best
of 3 vs captured baseline:

| workers | captured | ivar | delta |
|---|---|---|---|
| 1 | 3905 ms | 4091 ms | **+5 % (worse)** |
| 2 | 2258    | 2409    | +7 %             |
| 4 | 2048    | 2161    | +6 %             |
| 8 | 1989    | 2109    | +6 %             |
| 1→8 speedup | 1.96× | 1.94× | unchanged |

Hypothesis B refuted. Whatever drives the residual ceiling, it
is shared between `STORE_CAPTURED` and `STORE_INSTVAR` — i.e.
the dispatch / allocator / contention paths common to both,
not the captured-dict-specific code.

### A. The GC is NOT pausing the workers

Rebuilt protoCore with `-DPROTOCORE_GC_INSTRUMENT=ON` and
re-ran saturation with `PROTOCORE_GC_PROFILE=1`. Single run
per worker count:

| workers | drain | Phase 1 | Phase 5 (sweep) | GC cycles |
|---|---|---|---|---|
| 1 | 4111 ms | 3 794 513 µs (3.79 s) | 35 209 µs (35 ms) | **1** |
| 2 | 2592 ms | 2 404 045 µs (2.40 s) | 29 471 µs (29 ms) | **1** |
| 4 | 2719 ms | 2 548 687 µs (2.55 s) | 33 176 µs (33 ms) | **1** |
| 8 | 2493 ms | 2 270 868 µs (2.27 s) | 30 044 µs (30 ms) | **1** |

Only ONE GC cycle fires for the whole run, and the giant Phase-1
number is **not pause time for the mutators** — it is the time
the GC sat waiting for the workers to park. Workers never park
during a `drainOne` (no STW safepoint in the bytecode dispatch
loop), so the GC parks at the start (when the first refill
exhausts the freelist) and waits until the run ends for the
mutators to finally hit `workerSem.acquire`. The actual mark
(P2: 0.1-0.5 ms) and sweep (P5: ~ 30 ms) cost is tiny.

**This means**:
- The mutators run uninterrupted by the GC the entire benchmark.
- All memory is OS-allocated (posix_memalign) — the GC never
  recycles cells during the run. Total OS allocation = MaxRSS
  ≈ 1 GiB, stable across worker counts.
- The plateau is NOT the STW barrier as originally hypothesised.

## What IS likely the bottleneck

With STW and captured-dict-path ruled out, the residual ceiling
must be in the mutator path that scales with worker count.
Suspects:

1. **`getFreeCells` global-mutex contention.** Even though the
   batch size is large (60 K-65 K cells per refill), every
   refill serialises on a recursive mutex; the OS allocation is
   outside the lock, but the bookkeeping inside is not. With N
   workers all refilling on this benchmark's allocation rate,
   the mutex becomes a serialiser. Test: measure refill count
   per run and lock-acquisition wait time.

2. **CAS contention on shared scheduler state** — the ready
   stack head pointer, the actor's `__mailbox__` setAttribute,
   the actor lock. Under N workers all competing for the next
   actor + the next message inside that actor, cache lines
   bounce between cores at line-rate.

3. **False sharing on per-thread bookkeeping**. Atomic counters
   (`runningThreads`, `parkedThreads`, `gcCycleCount`) that live
   on the same cache line get rewritten on every worker
   transition.

The clean way to localise this is `perf stat -e
context-switches,cpu-migrations,cache-misses,task-clock` on a
single saturation run per worker count. The cache-miss /
context-switch curves vs worker count tell us which path is the
serialiser.

## Note on the GC-cycles-per-run profile flag

The instrumentation prints every cycle by default
(`(cycles > 0)` predicate in `gcThreadLoop`). Pre-edit it
printed every 5 cycles, which suppressed output for short
benchmarks like ours. Revert before merging into protoCore if
the per-cycle print floods stderr on a long-running test.

## perf stat + perf record found the culprit: createSymbol in every SEND

`perf stat` at workers=8 showed system time at 8.63 s vs user time
of 6.02 s — 60 % of the run in the kernel — with 585 K
context-switches in 2.75 s. That ruled out the GC and the allocator
and pointed at futex churn.

`perf record -g --call-graph=dwarf` then surfaced the actual hot
caller: **51 % of CPU was in `proto::SymbolTable::intern`**, of which
~ 24 % was `std::mutex::lock` and ~ 24 % in `lll_lock_wait` /
`futex_wait`. The callers were all on the SEND dispatch path in
`runLoop`.

Source: `ExecutionEngine.cpp` lines ~ 1207-1210 called
`ProtoString::createSymbol(ctx, "__class_side__")` and
`createSymbol(ctx, "__class_name__")` **on every SEND_** dispatch
to filter out class-side methods. Both strings are longer than 6
bytes so they skip the inline-string fast path and hit the
SymbolTable shard lock. Under 8-worker saturation that's
~ 5 000 SENDs/work × 160 work() = 1.6 M intern calls, all racing
on the same shard (because the string content is identical).

Same pattern, smaller magnitude, at line ~ 1073 for the super-send
class-name lookup.

## Fix: cache the two keys on `Bootstrap.sym`

Added `className` and `classSide` to `Bootstrap::Symbols`, interned
once at bootstrap, read directly in the SEND filter. Mirrors the
discipline already used for `mailbox`, `wrapped`, `selector`, …

Sweep result, best of 3, same machine + cgroup:

| workers | drain pre-fix | drain post-fix | improvement |
|---|---|---|---|
| 1 | 3987 ms | **697 ms** | **5.7×** |
| 2 | 2387 ms | **418 ms** | **5.7×** |
| 4 | 2373 ms | **218 ms** | **10.9×** |
| 8 | 2201 ms | **244 ms** | **9.0×** |

Speedup curve also recovers strongly:

| workers | speedup vs w=1 (post-fix) |
|---|---|
| 1 | 1.00× |
| 2 | 1.67× |
| 4 | **3.20×** |
| 8 | **2.86×** |

w=8 has a small regression vs w=4 (244 vs 218 ms) — that is the
new ceiling. But w=4 = 3.20× is now within shouting distance of the
ideal 4×, where the pre-fix run was ~ 1.7× and stuck. The earlier
~ 2× plateau was almost entirely a single mutex; with it gone the
mutator path scales nearly linearly through w=4.

MaxRSS also dropped from ~ 1.0 GiB to ~ 220 MiB — the per-SEND
`createSymbol` calls were building a fresh `ProtoStringImplementation`
rope each time and throwing it away (only the canonical interned
symbol gets retained); with the cache there is one allocation per
key for the whole runtime, not one per SEND.

## Policy fix in protoCore (`ea2c17f4`)

After the (A) measurement showed that the GC's Phase-1 wait was
dead time rather than mutator pause time, the trigger itself
was removed from `getFreeCells`. Old policy: every freelist
exhaustion unconditionally fired the GC, regardless of cap or
heap size. New policy: `getFreeCells` does NOT trigger; the GC
fires only when a soft/hard cap path needs it
(`reclaimWaitLocked`, `waitForHeapHeadroom`), when application
code calls `triggerGC()` explicitly, or at shutdown.

Behavioural change on the saturation workload (best of 3):

| workers | with trigger | without trigger | delta |
|---|---|---|---|
| 1 | 3987 ms | 4129 ms | +4 % |
| 2 | 2387    | 2457    | +3 % |
| 4 | 2373    | 2323    | -2 % |
| 8 | 2201    | 2272    | +3 % |

Identical within noise. MaxRSS still ~ 1 GiB. `PROTOCORE_GC_PROFILE=1`
now prints zero `[GC-PROFILE]` lines for this benchmark — there
are no cycles to print, which is the point.

Confirms what the profile already showed: the late sweep that
the spurious trigger produced was returning ~ 30 ms of memory at
the end of the run, too late to help any drain. The new policy
removes dead work without sacrificing throughput. Capped
configurations are unchanged: the soft/hard paths still trigger
the GC via `reclaimWaitLocked`. **The change is exclusively in
the default (uncapped) case.**

This also clarifies the residual ~ 2× plateau diagnosis: with
the GC out of the picture entirely, the plateau must live in
the mutator path — `posix_memalign` / glibc-arena contention
under N-worker refills, CAS contention on scheduler state,
or false sharing. The original suspects list above still stands;
the GC has been definitively excluded.

## Optimisations landed during this session (post-plateau hunt)

Two SymbolTable / dispatch-path fixes that, together, lifted the
saturation 8-worker scaling from regression at 1.96 → 3.83× and
the headline throughput from 30 → 62 K msg/s on mt100a:

### 1. `__class_name__` / `__class_side__` cached on Bootstrap (`ea35db9`)

`perf record` on saturation w=8 showed 51 % of CPU in
`SymbolTable::intern` + its shard mutex (futex_wait). Traced to
two `createSymbol(ctx, "__class_…")` calls in the SEND_*
dispatch class-side filter — both strings > 6 bytes so they
skip the inline path and hit the same shard each time. Cached
on `Bootstrap::Symbols`, same per-space discipline as the
other hot keys.

### 2. `newFuture` → 1 setAttribute; envelope built immutable (`5523bf8`)

The next perf pass showed the new hot was `allocCell + implSetAt`
during SEND fast-path COW. Two cuts:
- `newFuture` only stamps `__state__` now; `__value__` and
  `__error__` default to PROTO_NONE through the prototype
  chain until the resolve/reject paths write them.
- SEND envelope is now `newChild(isMutable=false)`. Each
  setAttribute on an immutable cell allocates 2 cells (vs 3 for
  mutable) and removes the shard-root CAS + global mutex from
  the path entirely.

mt100a w=1: 47 K → 54 K → 62 K msg/s across these two commits.

## The remaining sub-linear scaling is in the dispatch path itself

After both fixes, saturation 8-worker is at 0.18 s wall vs 0.69 s
w=1 = 3.83× (ideal 8×). Confirmed root with `perf stat`:

  w=1: 6.67 G instructions, IPC 2.49, L1-miss 0.49 %
  w=8: 6.69 G instructions, IPC 1.65, L1-miss 0.93 %

**Same instruction count, lower IPC, double the L1 misses** —
classic cache-line bouncing, not CAS-retry-loops. `perf record`
on `cache-misses` flagged `getFreeCells → atomic store on
Cell::next` during freelist refill as the top miss site (8 % of
sampled misses).

We then ran the inverse test: saturation with the inner work
collapsed to a single `count := count + 1` (zero captured-var
allocations). If allocation pressure were the cause, this should
scale much better. It DOES NOT — it plateaus at 2.69× at w=8
(slightly WORSE than the alloc-heavy version's 3.83×).

The lesson: **the residual ceiling lives in the dispatch
infrastructure**, not in user work or in the allocator. When the
inner work is heavy, dispatch is a small % of CPU and workers
stay on local caches; when the inner work is light, the dispatch
share rises and contention shows. The dispatch-side suspects, in
order:

1. **mailbox COW per SEND** — every SEND does
   `setAttributeIfEqual(__mailbox__, old, new)` with CAS-retry on
   the actor's mutable shard root. N senders to N different
   actors map to N (probably distinct) shards, but the cache lines
   for the SparseList COW result are shared.

2. **`ReadyStack` head CAS** — 8 workers all competing on a
   single Treiber-stack head pointer. Each `dequeueReady` is a
   CAS-pop; under contention the line bounces.

3. **per-actor `__sched__` 3-state flag CAS** in `finishDrain` /
   `enqueueReady` — every actor turn touches this once.

Attacking any of these is non-trivial (work-stealing per-worker
ready queues, intrusive lock-free mailbox lists, etc.) — material
for a separate session, not a single-commit fix.

## Throughput summary at session end

| benchmark | pre-session | end of session | best speedup |
|---|---|---|---|
| mt100k w=1 | ~ 20.6 K | **36.0 K msg/s** | 1.75× |
| mt100a w=1 | ~ 29.6 K | **64.5 K msg/s** | **2.18×** |
| saturation w=8 | 1.96× (regression) | **3.83×** | monotonic |

mt100a NO longer regresses with workers but it does not improve
either (49 K at w=8 due to producer cuello — single main thread
generating the SENDs while 8 workers compete for them). Multi-
producer benchmarks via driver actors are blocked by the
yieldable-do: limitation documented in
`2026-05-23-multiproducer-blocker.md` and remain the natural
next experiment once that lift lands.
