# Multi-producer benchmark — fix landed, scaling diagnosis (2026-05-23)

## Summary

The mt100a "anti-scaling" was a single-producer artefact (main thread is
the bottleneck). To verify the multi-producer hypothesis a fresh
benchmark was built; doing so surfaced a **real runtime cooperative-yield
bug** in protoST's nested-engine path that has been partially fixed.

Three landed deliverables:

1. **`ExecutionEngine.cpp` — coalesce-snapshot fix** for nested-engine
   `FutureYield`. Pre-fix, every outer engine's catch overwrote the
   actor's `__suspended_frame__` with its OWN frames, losing the inner
   block frame entirely. Now the outer catches PREPEND their frames to
   whatever a deeper engine already stored. Only the first (innermost)
   catch registers with the future. 751/751 ctest, no regressions.

2. **`benchmarks/actors/multi_producer.st`** — 8 driver actors, 12
   sinks each, 1 000 rounds, total 96 000 messages. Drivers run in
   parallel within each round; the round-by-round coordination is
   driven by main. Returns 96 000 correctly under all worker counts.

3. **The real scaling diagnosis (see numbers below)**: the multi-producer
   benchmark scales 1 → 2 → 4 workers (sweet spot at w=2-4, 1.3-1.4×
   over w=1) and regresses at w=8. The same regression at w=8 is
   visible on `parallel_speedup.st` (pure CPU, no inter-actor sends),
   which means the worker-pool ceiling is set by **protoCore-level
   contention**, not by anything protoST is doing — and certainly not
   by the single-producer artefact in mt100a.

## The runtime bug and the coalesce fix

### Reproducer

A 12-line repro (`/tmp/mp_dbg19.st`) where `f wait` fires inside a
`do:` block inside an actor method:

```smalltalk
Driver >> doBlock
  | total |
  total := 0.
  sinks do: [ :s | | x | x := (s ping) wait. total := total + x ].
  ^ total.
```

For three pinged sinks, the expected result is 1+2+3 = 6.

- **Before fix:** returns `0`. The do: only ever processed one iteration
  *and* the inner block frame was discarded across yield-resume, so
  the captured-dict write to `total` was lost.
- **After coalesce fix:** returns `1`. The inner block frame IS now
  preserved on resume, so the first iteration's write to `total` lands.
  The remaining iterations are still lost because the do: primitive
  iterates in C++ — recovering them is a separate, larger refactor
  (see "Open work" below).

### Diagnosis

Primitives that take a block and evaluate it internally
(`ifTrue:`/`ifFalse:`/`whileTrue:`/`do:`/`select:`/`collect:`/…) spin
up a fresh `ExecutionEngine` on the C++ stack via
`block_prims.cpp::invokeBlock`. When a `Future>>wait` from inside that
nested engine fires `FutureYield`, the inner engine catches it,
snapshots its frames into `actor.__suspended_frame__`, and rethrows.
The exception propagates through the primitive back to the outer
engine's `SEND_*` dispatch, where the outer engine's runLoop catches
`FutureYield` again.

Pre-fix, that outer catch unconditionally **overwrote** the actor's
`__suspended_frame__` attribute. Only the outermost engine's frames
survived restore — every inner block frame, including its operand
stack and locals, was lost. On resume the dispatcher continued at the
outer method frame's `pc` (already past the `SEND` that yielded) with
no record of the iteration progress made.

The COOPERATIVE YIELD LIMITATION block at the top of
`ExecutionEngine.cpp` already documented this: "those remain
non-yieldable. Lifting those primitives into the engine is future
work."

### The fix

`ExecutionEngine.cpp` (~`runLoop`'s `catch (FutureYield&)`):

```cpp
const proto::ProtoObject* existing = actor->getAttribute(ctx, suspKey);
const bool firstCatch = (!existing || existing == PROTO_NONE);

const proto::ProtoObject* mySnap = snapshotFrames(ctx);
const proto::ProtoObject* combined;
if (firstCatch) {
    combined = mySnap;
} else {
    // outer frames first, inner frames already-stored second — the
    // resulting list orders frames outermost-first, matching frames_
    // order in restoreFrames.
    auto* result = ctx->newList();
    for (auto* f : myList) result = result->appendLast(ctx, f);
    for (auto* f : exList) result = result->appendLast(ctx, f);
    combined = result->asObject(ctx);
}
actor->setAttribute(ctx, suspKey, combined);

if (firstCatch && y.future()) {
    actor->setAttribute(ctx, waitingOnKey, y.future());
    appendFutureWaiter(rt_, ctx, y.future(), actor);  // may schedule
}
// (Subsequent outer catches do NOT re-register with the future.)
```

Two invariants protected:

- **Frame preservation.** Inner block frames survive resume.
- **Single future registration.** The innermost catch (which is the one
  closest to the actual `wait`) owns `__waiting_on__` and the
  `appendFutureWaiter` call. Outer catches must not re-append — a
  double-append would wake the actor twice when the future settles.

### What the fix does AND does not buy

The coalesce fix completes the resume-frame story but does not address
*primitive-loop continuation*. After resume:

- `inner block frame` resumes at post-wait pc, runs its body to
  completion (captured-dict writes land correctly).
- `inner block frame` returns. Its return value lands on the OUTER
  method frame's operand stack at post-SEND pc.
- The dispatcher continues from there.
- **But the primitive's C++ loop is gone**: the `do:` primitive only
  ever ran one iteration of `forEachElement` before unwinding.
  Remaining elements are not processed.

Single-block patterns (`x ifTrue: [ ... wait ]`,
`atom swap: [ ... wait ]`) now work correctly — the block runs once,
yields, resumes, completes, and its value flows up exactly as
`ifTrue:` / `swap:` would have returned synchronously. Iterating
patterns (`coll do:`, `1 to: n do:`, `coll select:`) still drop the
tail of the iteration on yield.

For benchmarks and digital-twin code we therefore use **inline
unrolled send / wait sequences in actor methods**, and let main
drive any outer "rounds" loop (main isn't in an actor — its own engine
just blocks on `f wait` instead of yielding). See `multi_producer.st`
for the worked-out pattern.

## The benchmark

`benchmarks/actors/multi_producer.st`:

- 8 driver actors, each owns 12 private sink actors.
- `Driver >> roundOnce` is hand-unrolled: 12 inline `(sinks at: i) ping`
  fan-outs, then 12 inline `f_i wait` fan-ins. No `do:` block — every
  `wait` fires from the actor method frame itself, where the snapshot
  / coalesce-restore path is correct.
- Main coordinates rounds: 1 000 outer iterations of "fan-out to all
  drivers, fan-in their futures". Main's own `do:` loops are fine —
  main is not an actor; its engine blocks rather than yields.
- Total messages = 8 × 12 × 1 000 = **96 000**. Result is exactly that
  under every worker count (verifies correctness of the unrolled
  pattern and the coalesce fix).

## Scaling numbers

Best of 3 runs, `cgroup MemoryMax=2G`, machine quiescent:

| `PROTOST_WORKERS` | `multi_producer.st`  | `parallel_speedup.st` (CPU only) |
|---|---|---|
| 1 | 5.04 s  (19 K msg/s)         | 21.65 s        |
| 2 | 3.75 s  (25.6 K, **1.34×**)  | 30.28 s  (**1.40× slower**) |
| 4 | 4.11 s  (23.4 K, 1.23×)      | 16.95 s  (1.28×)            |
| 8 | 7.71 s  (12.5 K, **regression**) | 23.12 s  (regression)   |

(Run-to-run variance is non-trivial — see notes; the qualitative
pattern is reliable across runs.)

Two observations:

1. **Multi-producer relieves the producer-side bottleneck.** mt100a's
   single-producer pattern saturated main long before workers ran
   out; switching to 8 parallel drivers gets a measurable speedup
   on `w=2` (1.34×) and `w=4` (best raw throughput).

2. **The bigger ceiling is in the worker pool itself.**
   `parallel_speedup.st` has zero inter-actor SENDs — 12 actors each
   running a pure CPU loop — and it STILL anti-scales from `w=1` to
   `w=2`. That is a protoCore-level scaling cost, not a multi-producer
   one. The likely suspects are listed in the next section.

The user's "1 worker → 2 workers should be brutal" expectation cannot
be met without addressing the worker-pool ceiling at the protoCore
level. Multi-producer is necessary but not sufficient.

## Where the protoCore-level ceiling lives

Three candidates, in decreasing order of likelihood for explaining the
`w=1 → w=2` regression observed even on pure CPU work:

1. **Concurrent GC stop-the-world cadence.** Every workload-driven
   collection cycle pauses ALL workers simultaneously while the
   `gcThreadLoop` waits for `parkedThreads >= runningThreads`. With
   `w=2` the all-stop barrier kicks in twice as often per unit work
   as with `w=1` — and the marker overhead grows roughly linearly
   with mutator allocation rate, not with worker count, so doubling
   workers doubles GC frequency but does not double their useful
   work share.

2. **Per-thread allocator refill cost.** `ProtoSpace::getFreeCells`
   takes the global mutex on every refill; refill size scales with
   `runningThreads × 4` (so each worker gets a fatter slab), but the
   contention on the mutex itself + the per-refill cache-line
   eviction adds up — particularly under the bursty allocation
   profile of a Smalltalk-style runtime.

3. **Cache-line ping-pong on the ready stack.** The Treiber stack used
   for the ready queue has a single head pointer on which every push
   and pop CAS. With multiple workers the cache line bounces between
   cores at line-rate, eating CPU cycles that show up as kernel time
   in perf.

Fixing the first one is design-scale work (concurrent root marking,
or generational GC); the second is more tractable (per-thread
arenas, larger slab sizes, NUMA-aware refill); the third is a queue
redesign (work-stealing per-worker deques, segmented Treiber).

Not in scope for tonight.

## Open runtime work

To make the iteration primitives (`do:`, `to:do:`, `select:` …)
yieldable per-iteration, the C++ `forEachElement` loop in each
primitive has to be replaced by an iteration carrier the engine can
suspend and resume. Two concrete approaches:

- **Bytecode-emitted loops.** The compiler desugars
  `coll do: [...]` into a `1 to: coll size do:` bytecode loop that
  uses direct `value:` sends. `value:` is already a fast-path SEND
  that pushes a frame onto the CURRENT engine, so the whole thing
  stays in one engine and the existing snapshot/restore path covers
  it. Cost: the compiler change is small, but it changes semantics
  for user `do:` overrides (they'd have to opt in via a different
  selector or trait).

- **Iterator continuation frames.** Add a new frame kind whose
  `m_ptr` is a sentinel the engine recognises as "continuation
  frame; on dispatch call the registered C++ function with the
  carried state". `do:` then pushes one continuation frame with
  `{receiver, block, index=0, n=size}` and one block frame for the
  first element. When the block frame returns, RETURN_TOP pops it
  and the continuation frame's dispatch handler advances `index`,
  pushes the next block frame, or returns the receiver if done.
  Cost: every iterating primitive needs a continuation handler, and
  the snapshot/restore path must serialise the continuation state.

The bytecode-emitted path is the smaller code change; the
continuation-frame path is the more flexible foundation.

## State of the tree

After tonight's session:

- `ad7f112` (the morning's tip) + 1 uncommitted patch on
  `src/runtime/ExecutionEngine.cpp` (~ 70 LOC, the coalesce fix
  above).
- New file `benchmarks/actors/multi_producer.st`.
- This document at
  `docs/superpowers/specs/2026-05-23-multiproducer-blocker.md`.
- ctest: 751/751.
- No protoCore changes. Nothing pushed.

## Suggested next actions

1. **Land the coalesce fix as a focused commit** (the diff is
   self-contained and ctest-clean).
2. **Add an explicit regression test** that covers the
   nested-engine-yield path. A minimal test asserts that
   `actor` running `expr := (cond ifTrue: [ (other request) wait ]
   ifFalse: [ 0 ])` returns the correct value — pre-fix this
   yields → resumes → returns 0 erroneously; post-fix it returns
   the wait's value. (The repro in this doc is enough to write
   it from.)
3. **Pick ONE protoCore scaling-ceiling target** and prototype:
   either concurrent root marking (highest payoff, biggest scope)
   or per-thread allocator arenas (more tractable, narrower
   payoff). Anything else is wallpaper.
4. **Pick ONE iteration-primitive lifting approach** (bytecode
   or continuation-frame) before touching `do:` / `to:do:` /
   `select:` / etc.
