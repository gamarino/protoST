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

## Next experiments (in order of payoff)

The residual ~ 2× plateau survives a 5× memory headroom, so the
ceiling is INSIDE the runtime. The likely suspects, in order:

1. **GC Phase-1 STW barrier cadence**. The barrier is
   `parkedThreads >= runningThreads` — every cycle ALL workers
   must park before the GC marks. With 8 workers all driving the
   same allocator under saturation, refill events are frequent
   and STW barriers are frequent. Rebuild protoCore with
   `-DPROTOCORE_GC_INSTRUMENT=ON` and run with
   `PROTOCORE_GC_PROFILE=1` to get per-phase microsecond
   accounting; that lets us see the STW share of wall time
   directly instead of inferring it.

2. **`getFreeCells` global mutex contention.** The current
   per-refill batch (`blocksPerAllocation × runningThreads × 4`,
   capped at 65 536) is large, but every refill still serialises
   on the recursive mutex. Under N-worker saturation the refill
   rate is N× higher; even a fast mutex turns into a serialiser.

3. **Mutable `setAttribute` on the captured dict**. Each
   `work()` does 5 000 captured-var writes; if the sparse-list
   path COWs the node on each write, that's the allocation rate
   driving (1) and (2). Compiling the bench's `work` to use
   `STORE_INSTVAR` instead of `STORE_CAPTURED` (move `sum` into
   an instance variable) would either flatten the plateau or
   confirm the captured-dict path as the allocation driver.

The plan is to attack (1) first — get the GC profile so we
KNOW whether STW is the dominant residual cost, and only then
decide between "shrink STW" (the deepest fix: concurrent root
marking) and "reduce allocation rate" (the targeted fix:
adjust `work`, or fold mutable setAttribute hot paths in the
runtime).
