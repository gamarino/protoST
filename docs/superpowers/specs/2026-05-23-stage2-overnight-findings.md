# Stage 2 overnight findings — the FIFO blocker

**Status:** investigation complete, implementation reverted, recommendation
attached. 2026-05-23 overnight session.

## What I tried

Stage 2 option (B), as agreed earlier:

- Replace per-actor `__mailbox__` + global ready-queue with **one** global
  ProtoList of tasks under `liveRegistry.__tasks__`.
- Each task is a `ProtoObject` carrying `__actor__`, `__selector__`,
  `__args__`, `__future__` (and optionally `__resume__` for resume-from-yield
  tasks).
- Per-actor blocking lock = `binary_semaphore` wrapped in ExternalPointer
  under `__lockHandle__` (allocated in `asActor`).
- Cooperative yield preserved via per-actor `__deferred__` list: while an
  actor is suspended on a future, new tasks targeting it get deferred to
  `actor.__deferred__`; on resume completion, drain back to `__tasks__`.

I implemented both variants (blocking-lock and non-blocking-`try_acquire`) of
the dispatch loop. Both ctest-passed compilation and the smoke `mt2k`.

## The blocker: per-actor FIFO cannot be preserved

### Variant 1: blocking `acquire()`

Three workers pop tasks T1, T2, T3 (in FIFO order from `__tasks__`). All three
call `lock.acquire()`. Order of "first to reach the kernel futex_wait" is
**not deterministic** under multi-core scheduling: depending on which core is
faster to context-switch through the syscall, any of the three can be first
in the futex wait list. The futex itself wakes in FIFO **of the wait list**,
not of pop order. So even with strict-FIFO futex, the message run order
is permuted.

This caused 7-10 test failures in tests like `01_async_and_wait` (expected
30, got 15 — the value-read message ran before the third increment).

### Variant 2: non-blocking `try_acquire()` + defer-to-deferred-on-miss

Workers pop tasks T1, T2 in FIFO. Worker A try-acquires → success. Worker B
try-acquires → fail, defers T2 to `actor.__deferred__`. Worker A processes
T1, then drains `actor.__deferred__` and processes T2. **Looks FIFO.**

But same race the other way: worker B happens to win the try-acquire (cache
warm, no migration, etc.) → B processes T2 first, A defers T1, B drains
deferred and processes T1. **Order processed: T2, T1.** FIFO violated.

The fundamental issue: **the pop is atomic, but pop+lock together is not.**
Between the two ops, scheduling can reorder which worker "wins" the lock.

## What this means

A naive "global task list + per-actor lock" architecture **cannot preserve
per-actor FIFO ordering under multi-worker contention** without one of:

1. **A single popper thread** — serializes pops, dispatches actors to workers.
   Defeats parallel pop (workers wait for the popper).
2. **A pop-and-lock atomic primitive** — would require non-trivial CAS-over-
   two-things, or per-actor reservation slots in the queue. Significant new
   protoCore extension.
3. **Pre-pop per-actor serialization** — sender does a CAS on the actor's
   busy flag; if it transitions 0→1, the sender pushes the actor to a global
   "ready actors" list and stashes the task in a per-actor mailbox; if the
   flag is already 1, the sender just appends the task to the per-actor
   mailbox. This is **exactly the current d4d3db8 architecture** — it has
   per-actor mailbox *because* of this requirement.
4. **Worker pinning** — hash the actor pointer to a worker, route all of its
   tasks to that worker. Loses load balance and yields strange interactions
   with `wait` (a worker blocked on a future cannot drain other tasks).

## The original architecture is right (for FIFO)

The "second indirection" (per-actor mailbox + global ready) that you wanted
to remove **is what makes FIFO efficient**. The sender's atomic dance —
"CAS the actor's busy flag; if I won, also push the actor to the global
queue" — encodes per-actor serialization at the source, not at the consumer.
This is the same pattern Akka, Erlang BEAM, Tokio etc. use, for the same
reason.

## What is salvageable

The per-actor blocking lock infrastructure (`ActorLock` + binary_semaphore
+ `attachActorLock`/`acquire`/`release`/`tryAcquire`) is a clean primitive
that survives any direction.

The `__deferred__` symbol and the helper functions (`popFirstFromList`,
`appendToList`) are general-purpose ProtoList utilities — useful regardless.

The Bootstrap symbol prep (`tasks`, `actor`, `lockHandle`, `resume`,
`deferred`) is harmless and ready for whichever future direction we choose.

All of these are already in commit `12ba983` (dormant). Nothing to redo.

## Recommendation

**The d4d3db8 architecture is correct in concept** (per-actor mailbox + global
ready queue + event-driven semaphore wake). The remaining performance ceiling
comes from:

1. **Per-message allocation**: each SEND allocates an envelope ProtoObject
   (3-4 sparse-list rebuilds for setAttribute) + a Future ProtoObject
   (state/value/error setAttributes). 5-8 allocations per send.
2. **Mailbox `ProtoList` CAS-rebuild**: 1 alloc per send (mailbox AVL update).
3. **Future settlement**: 5+ setAttribute calls (state, value, drained
   __waiters__, drained __then_cbs__, etc.) = 5+ sparse-list rebuilds.

The real Stage 2 win lies in **reducing per-message allocations**, not
in restructuring the dispatch shape. Concretely:

- **Pool envelope objects**: reuse envelope ProtoObjects across messages.
- **Pool Future objects**: reuse settled Futures.
- **Flat Future state**: a single `std::atomic<int>` in a side struct
  (one per Future, ExternalPointer-attached) replaces the multi-setAttribute
  state-machine. settle = atomic CAS; wait = atomic.wait/notify_all.
- **Mailbox as intrusive linked list**: still per-actor, but C++ struct
  nodes (no AVL rebuild per send). Requires the GC-tracing extension to
  protoCore (`OpaqueGCRoot` with custom trace callback) — that's a
  protoCore v1.2.0 bump.

These are the items in the original Stage 2 spec (`docs/superpowers/specs/
2026-05-23-interpreter-perf-spec.md`) that I deferred when I went after
the dispatch structure first. The dispatch structure is fine. **The
allocation overhead is the real ceiling.**

## State left

- Repo at `12ba983` (clean, 751/751 ctest, ~10s).
- This document under `docs/superpowers/specs/`.
- No protoCore changes.
- No regressions.

## For tomorrow

I suggest discussing in order:

1. **Validate the FIFO finding** — independently. The argument above might
   have a flaw I missed under fatigue.
2. **Decide direction**: stay with d4d3db8's dispatch architecture; pursue
   the per-message allocation reduction (pool + flat Future + intrusive
   mailbox). The first two are protoST-only. The third needs protoCore.
3. **If protoCore extension chosen**, design `OpaqueGCRoot` as a clean
   primitive (one source file, one spec) before any flatten work.

Realistic gain target with pooling + flat Future + intrusive mailbox:
mt8w workers=8 should drop from ~7.6s to ~1-2s (true parallel scaling at
last). mt100k single-actor stays similar (~5s — the parallel ceiling is
already irrelevant for one actor).
