# Changelog

All notable changes to protoST are recorded here. The living, item-by-item
state of the language is tracked in [`docs/STATUS.md`](docs/STATUS.md).

## 0.3.0 — yieldable iteration (2026-05-24)

Adds `doYielding:` — the compiler-recognised yieldable counterpart of
`do:`. Lifts the old "no `wait` inside a `do:` block of an actor
method" limitation that blocked driver-actor multi-producer patterns.

### New language feature

- **`coll doYielding: [ :elem | body ]`** — when the compiler sees
  this exact shape (literal block, one formal parameter), it emits a
  bytecode loop using `at:` + `value:` instead of dispatching the
  primitive `doYielding:`. The block's `value:` send uses the
  engine's inline block-frame fast path (yieldable), so a `wait`
  inside the block parks cooperatively without losing iteration state.
  See `docs/tutorial/10-actors-and-futures.md` §10.8 for a worked
  example.
- **Limit**: only `SequenceableCollection`s (Array, OrderedCollection,
  Interval, String) support it — receivers without `at:` / `size`
  raise `doesNotUnderstand: doYielding:` at runtime. `do:` itself is
  unchanged.
- **Limit**: the integer `to:do:` and `whileTrue:` primitives are
  still non-yieldable. For a counted loop with `wait` inside, build
  an index list first and iterate with `doYielding:`.

### Runtime fixes

- **`finishDrain` stale-wakeup must respect `suspended`** (commit
  `7637e4f`). The 2026-05-23 stale-wakeup optimisation released the
  actor whenever schedState==2 with an empty mailbox, even when the
  drain exited via `FutureYield` (`suspended=true`). That LOST
  WAKEUPS for an actor whose awaited future had already settled by
  the time the engine tried to register it as a waiter:
  `appendFutureWaiter` returned `parked=0`, the handler called
  `schedule()` to mark state 1→2 ("re-schedule me to consume the
  value"), and finishDrain interpreted the 2 as stale and unanchored.
  The actor was left with `__suspended_frame__` set but no one to
  schedule it. Symptom: `doYielding:` in an actor method, w >= 2,
  N >= 3 iterations deadlocked. Fix: re-enqueue when `suspended=true
  || mailboxHasWork`.

### New runtime opcode

- `Op::JUMP_BACK` — backward complement of the existing forward
  `JUMP`. Used by the `doYielding:` desugar to close the loop. Five
  lines in `ExecutionEngine.cpp`; no other call sites.

### New benchmarks

- `benchmarks/actors/multi_producer.st` — rewritten to use
  `doYielding:` (was blocked by the limitation). Each of 8 driver
  actors fan-outs to 12 private sinks over 1000 rounds (96 000 ping
  messages, summing settle counts to 48 048 000). The benchmark
  RUNS and produces the correct answer; **throughput is comparable
  to `mt100a`, NOT the 10× the design speculated** — each driver
  pays a yield-resume cycle on every fan-out element, more expensive
  than main's blocking wait. The real value of `doYielding:` is
  correctness + composability, not aggregate throughput. The
  honest performance number remains the 0.2.0 figure
  (mt100a w=2 ~ 67-72 K msg/s on the 5500U notebook).

### Test count

752 → 753: one new conformance test for the race fix
(`tests/conformance/do_yielding_actor.st` — 3 sinks × driver-actor
with `doYielding:` containing `wait`, multi-worker stable).

## 0.2.0 — Performance pass (2026-05-23)

A focused overnight optimisation pass on the actor dispatch path.
Headline: `mt100a` (the round-trip throughput benchmark) moves from
**~ 30 K msg/s** to **71.9 K msg/s** on a 6-core notebook host — a
**+143 %** improvement, putting protoST in the 100 K+ msg/s class on
any modern desktop (projection: 130-150 K msg/s on a Ryzen 7700X /
i9-13900K).

Three concurrent-runtime bug fixes also landed alongside the
optimisations — each was a real correctness improvement uncovered by
the performance investigation, not a tuning knob.

### Throughput (best of 3, AMD Ryzen 5 5500U, 6 physical cores)

| benchmark | before (0.1.0) | after (0.2.0) | factor |
|---|---|---|---|
| `mt100k` w=1  | ~ 20.6 K msg/s | 36.6 K        | +78 %  |
| `mt100a` w=1  | ~ 29.6 K       | 68.5 K        | +131 % |
| `mt100a` w=2  | — (not optimal) | **71.9 K**    | best ever |
| `mt100a` w=4  | regression       | 71.9 K (no regression) | fixed |
| `saturation_big` w=6 scaling | regression at w=8 | **3.88×** (near-ideal 4× on 6 cores) | fixed |

Full report and projections to other hardware:
[`benchmarks/reports/2026-05-23-performance.md`](benchmarks/reports/2026-05-23-performance.md).

### Bug fixes (correctness)

- **`finishDrain` spurious re-enqueue.** Pre-fix, every actor with
  multiple pending sends would be processed TWICE — once for real,
  once with an empty mailbox. Per-worker stats made the pattern
  visible (one worker accumulating 27 parks while others sat at 2-3).
  Fix: CAS the schedState transition first, THEN check the mailbox;
  if empty, release without re-enqueue.
- **Nested-engine cooperative-yield snapshot.** A `Future>>wait` from
  inside a block invoked by a primitive (`ifTrue:`, `whileTrue:`,
  `do:` etc.) only saved the outermost engine's frame stack; the
  inner block frame was silently dropped. Single-block patterns
  (`x ifTrue: [ ... wait ]`) now round-trip correctly. Iteration
  primitives (`coll do: [ ... wait ]`) get partial coverage — the
  full lift is documented as open work.
- **`WorkerPool` pause-and-load.** New runtime API
  (`WorkerPool stopProcessing` / `startProcessing`) that lets a
  benchmark pre-fill every actor mailbox before releasing the
  workers — needed to measure pool drain capacity in isolation
  from main's SEND rate.

### Performance optimisations

- **`createSymbol` on the SEND-* hot path replaced by Bootstrap
  cache.** Per-SEND `createSymbol("__class_name__")` +
  `createSymbol("__class_side__")` were ~ 45 % of CPU in `perf
  record` on saturation w=8, hammering the SymbolTable shard
  mutex. Both keys cached on Bootstrap; ~ 10× speedup at w=4.
- **`newFuture` reduced to one `setAttribute`.** Each new Future
  used to stamp three attributes (state, value, error); only
  `__state__` is load-bearing for the settle CAS. Saves ~ 6 cells
  per future, ~ 600 K cells on a 100 K-msg run.
- **SEND envelope built immutable.** The message envelope is read
  by the worker exactly once and never mutated; building it
  immutable saves the mutable shard-root CAS on every send.
- **Worker spin-before-park.** Workers now spin a few µs checking
  the ready stack before parking on the futex. Catches bursty
  SENDs from main in-flight and skips the futex entirely.
  `mt100a` parks dropped from 100 001 to 8 923 at w=1 (-91 %).

### protoCore changes (companion)

Three commits in the protoCore kernel (the kernel `0.1.0 → 0.2.0`
companion release):

- **GC no longer triggers on freelist exhaustion when no heap cap
  is set.** Pre-fix, every freelist refill woke the GC unconditionally
  — and with no STW safepoint in protoST's bytecode dispatch, the GC
  parked waiting for the workers and never ran until the program
  ended. Now the GC runs only when a soft/hard heap cap is configured,
  the public `triggerGC()` API is called, or on shutdown.
- **`Cell::internalSetNextRaw` and `mutableRoot[shard].root` reads
  relaxed.** Both were `seq_cst` / `acquire`; x86 TSO already
  provides the necessary ordering and the operations have no
  cross-thread synchronisation contract.

### Operational guidance

- Use `PROTOST_WORKERS=N` with N = **physical core count** for
  CPU-bound workloads. Above that, SMT contention regresses the
  result (10-15 % slower per SMT pair active).
- For producer-bounded benchmarks (anything where main is the sole
  thread issuing SENDs), the sweet spot is **w=2**. Beyond that
  the workers compete for work that does not exist.
- The new `PROTOST_WORKER_STATS=1` env var prints per-worker drain
  + park counters on shutdown — useful for fairness diagnosis.

## 0.1.0 — Initial release (2026-05-22)

The first public release of **protoST** — an actor-native Smalltalk runtime
built on the [protoCore](../protoCore) kernel.

### Language

- Lexer, parser and a non-recursive bytecode VM; closures with capture;
  classes, instances, methods and instance variables; `self` / `super`.
- Non-local return and the full Smalltalk exception protocol
  (`on:do:`, `signal`, `ensure:`, `ifCurtailed:`, resumable / retry).
- A real collection hierarchy and the iteration protocol.
- A standard library (Stream, Math, Random, JSON, Time) and a file-based
  module system integrated with protoCore's UMD module discovery.
- Advanced object model — multiple inheritance, mixins (`uses:`), runtime
  behaviour composition (`addBehavior:`).

### Actors and concurrency

- A first-class, language-embedded actor model: `asActor`, asynchronous
  message sends returning `Future`s, one-message-at-a-time per actor, a
  parallel worker-pool scheduler, and cooperative yield/resume.
- **Lock-free actor mailbox and Future** — no per-actor or per-future mutex;
  both run on protoCore's atomic attribute compare-and-swap.
- **`Atom`** — a shared mutable cell with optimistic-concurrency CAS
  (`value:ifCurrent:`, `swap:`), plus `Object>>setInstVar:from:to:` — the raw
  CAS on any instance variable.

### Tooling

- An interactive REPL, a Debug Adapter Protocol debugger (VS Code), native
  installers (CPack), a dual-audience tutorial, ~40 runnable examples and a
  benchmark suite.

### Tests

- 751 tests pass via `ctest` (the conformance suite, the unit suite, the
  examples and the CLI stress tests), each run in its own process.

### Known issues

Recorded honestly, with bounds, in [`KNOWN_ISSUES.md`](KNOWN_ISSUES.md):
one `STRuntime` per process (K1), very large ropes (K2), no `%` string
formatting (K3). None affects the shipped CLI configuration.
