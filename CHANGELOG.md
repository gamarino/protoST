# Changelog

All notable changes to protoST are recorded here. The living, item-by-item
state of the language is tracked in [`docs/STATUS.md`](docs/STATUS.md).

## [Unreleased]

Changes committed after the `v0.3.0` tag.

### Language

- **Call-form sends and method declarations** (commit `2af8c5c`).
  `recv name(p1, p2, k1 = v1)` sends and `Class >> name(p1, k1 = default)`
  declarations follow the protoCore method convention (positional arguments
  plus named arguments), so methods exposed by other protoCore runtimes are
  callable without selector mangling. Named-argument defaults are evaluated
  at call time. A class cannot host both a unary `>> bar` and a call-form
  `>> bar(...)`; actor receivers do not accept call-form sends yet.
- **Class variables** (commit `6b3cf39`). A non-empty
  `classVariableNames:` clause is honoured: each name is installed on the
  class and readable from instance and class methods, including in
  subclasses. Assignment is allowed only from class-side methods; an
  instance-side assignment is a compile-time error. Closes D19 at that scope.

### Actors

- **Three priority bands** (commit `3efb31d`). `asHighPriorityActor` and
  `asLowPriorityActor` join `asActor` (Medium, the default). Workers drain
  strictly by priority — High, then Medium, then Low — with the
  single-method invariant unchanged in every band. There is no starvation
  guard. See `docs/tutorial/10-actors-and-futures.md` §10.11 and
  `examples/actors/05_priority_bands.st`.

### Runtime

- **Blocking OS calls run inside `ProtoContext::UnmanagedScope`** (commit
  `481a413`): `Object>>sleep:`, REPL input and `:load` file reads, the DAP
  server's message read and the CLI debugger prompt. A garbage-collector
  stop-the-world phase no longer waits for a thread blocked in these calls.
- **`doesNotUnderstand:` errors name the receiver's class** (commit
  `0075449`), e.g. `doesNotUnderstand: fooBar (receiver class: SmallInteger)`.

### Performance

- The compiler inlines `ifTrue:`, `ifFalse:`, `ifTrue:ifFalse:` and
  `whileTrue:` with literal blocks (commit `0d396ca`), and
  `start to: end do: [:i | body]` with a literal one-argument block (commit
  `ea5c952`).
- SmallInteger fast-path opcodes for `+ - <= < >= > =` (commit `b623448`).
- Dictionary key hashes are canonicalised through the symbol table, which
  enables the rope-aware `,` concatenation fast path (commit `8dd93f2`).
- Cheaper frame setup (commits `ac1e8b4`, `2c0ca61`), a single-guard fast
  path for `on:do:` (commit `f6a1aa2`) and cached attribute-key symbols in
  the exception primitives (commit `dad27b9`).
- Dated reports for this work are in `benchmarks/reports/` (2026-05-24).

### Fixes

- **Heap corruption when several workers ran a method for the first time at
  the same moment** (D25). The per-module caches of interned selector
  symbols, instance-variable keys and parsed call-form descriptors were
  filled lazily without synchronisation; two threads could both reallocate a
  cache and one wrote into the buffer the other had freed. It showed up as
  intermittent `malloc(): unaligned tcache chunk detected` /
  `tcache_thread_shutdown()` aborts (for example in
  `examples/programs/traffic_intersection.st`), crashes, or an instance
  variable reading as `nil`. The caches are now fixed-size tables published
  once with a compare-and-swap and filled through atomics; call descriptors
  are published immutably. Regression tests: `cli_concurrent_first_call`
  and two `[bytecode][concurrency]` unit tests.
- **Memory corruption when an actor method contained a string literal longer
  than six bytes or a Float literal** (D26). Literals were allocated on the
  main thread's context whichever thread ran the method, so worker threads
  took cells from the main thread's unsynchronised free list; every run of
  such a program crashed, hung or printed garbled text. Literals are now
  allocated on the calling thread's context. `STRuntime::materialize` takes
  that context, and a module loaded through `loadModuleFromFile` runs on the
  caller's context through the new `runTopLevel(module, ctx)` overload.
  Regression tests: `conformance/10-actors/literal-string-in-actor-method.st`
  and `literal-float-in-actor-method.st`.
- **Use-after-free and ABA hazard in the scheduler's lock-free ready queue**
  (D27). `ReadyStack` freed a node on every pop, so a concurrent pop could
  read the successor of a node another worker had already freed, and an ABA
  interleaving (glibc's tcache hands a freed node straight back to the next
  allocation) could install a freed node as the queue head, losing actors and
  double-freeing memory. The old comment claiming glibc does not recycle freed
  pointers quickly was wrong. The stack now keeps its nodes in a type-stable
  pool with a lock-free free list and tags each head with a 32-bit generation
  counter, so a stale compare-and-swap always fails; it also no longer calls
  malloc/free per message. It moved to `src/runtime/ReadyStack.h`. Regression
  tests: two `[scheduler][readystack]` unit tests.
- **`Import from:` inside an actor method answered `module not found`** (S6).
  The module provider found its runtime through a thread-local pointer set
  only on the thread that constructed the runtime, so imports failed on every
  worker thread (and on the DAP debuggee thread). The provider now resolves
  the runtime from the calling context's `ProtoSpace`. Imports from several
  threads also exposed a race: actors importing the same not-yet-loaded module
  each ran its top level and received distinct module objects. The new
  `STRuntime::importModuleFile` runs a module's top level once per runtime;
  concurrent importers wait for that load and receive the same module object,
  a failed load is not cached, and an import cycle raises
  `cyclic module import: <path>` instead of waiting forever. Regression
  tests: `conformance/11-modules/import-from-actor-method.st`,
  `import-concurrent-runs-once.st`, `import-cycle-errors.st` and
  `import-cycle-concurrent.st`.
- **An error raised by an imported module's top level reached the importer's
  handler twice** (D28). The module's top level ran through the entry point
  meant for a script's own top level, which turns an unwind that escapes it
  into a new error: the importer's handler ran once with the module's error
  and again with `exception unwind: no matching on:do: handler activation`,
  and `return:`, `retry` and `pass` in that handler misbehaved the same way.
  A module's top level now runs without that conversion, so the unwind
  reaches the importer's frames: the handler runs once with the original
  error, handler actions behave as for any other error, and a nested import
  failure crosses both modules. A failed load is still not cached. The same
  path also freed a failed module's compiled code while classes it had
  already declared stayed global, so calling one of their methods crashed;
  the code is now retained whether or not the load completes. Regression
  tests: the `import-error-*.st` and `import-warning-resume.st` tests in
  `conformance/11-modules/`.
- `--help` and engine errors no longer show internal milestone labels such
  as "(F7)", "— F2" or "F2 limit". A send with more than 8 arguments now
  reports "send of #<selector> has <n> arguments; at most 8 are supported per
  send", and a non-unary send to a value attribute reports "#<selector>: the
  attribute is a value, not a method; only unary sends read value
  attributes".
- Inlined conditionals and loops reject non-Boolean receivers with
  `doesNotUnderstand:`, as the non-inlined sends do, through the new
  `ASSERT_BOOL_OR_DNU` opcode (commit `cf2ebc3`).
- **The compiler crashed on block literals in inlined `to:do:` bodies**
  (D29). A program such as `1 to: 2 do: [ :i | [ 1 ] value ]` or the nested
  loop `1 to: 2 do: [ :i | #(1 2) do: [ :x | s := s + x ] ]`, at top level or
  in a method, died before running (segmentation fault or floating-point
  exception), or on some heap layouts compiled silently through freed memory.
  The inlined loop held a reference into the compiler's scope stack while it
  compiled the body, and the scope pushed for any block literal could move
  that stack. The scope stack no longer moves its entries. A second defect
  in the same inlining is fixed too: when the enclosing method or module
  captured a variable with the same name as the loop variable, the loop body
  read that captured variable (`i := 5. b := [ i ]. s := 0. 1 to: 3 do:
  [ :i | s := s + i ]` summed to 15, not 6). Such a loop is now compiled as
  a real block send. Regression tests: the `to-do-*.st` and
  `inlined-control-nests-blocks.st` tests in `conformance/06-blocks/`, two
  tests in `conformance/07-non-local-return/` and two `on-do-inside-to-do-*`
  tests in `conformance/08-exceptions/`.
- **A stop-the-world phase could start while a thread still ran** (S4).
  protoST removed a blocked thread from protoCore's running-thread count
  (`GcSafeBlocking.h`) while its REPL, `sleep:`, DAP and debugger reads used
  protoCore's unmanaged regions, which count the thread as parked; a thread
  counted both ways let the collector's quorum complete early. The DAP
  debuggee also ran on a plain `std::thread` sharing the adapter thread's
  context, so an `evaluate` while stopped overwrote the debuggee's frame
  slots (`doesNotUnderstand: do: (receiver: nil)`). Every blocking wait —
  idle and paused workers, `Future>>wait` outside actors, a concurrent
  import's wait, the shutdown join, a debugger stop — now runs in an
  unmanaged region on the blocked thread's own context, and the runtime no
  longer writes protoCore's thread counters. The debuggee is a protoCore
  thread with its own context; the CLI debugger prompt and `print` use the
  halted thread's context. `GcSafeBlocking.h`, `GcSafeMutex.h` and the unused
  per-actor lock were removed. An idle worker submits its young generation
  before sleeping; a thread waiting outside an actor does so only when no
  primitive-created engine is below the wait. Regression tests: two `[s4]`
  unit tests, `cli_gc_shutdown_stress` and `cli_dap_gc`.
- **Comparisons with NaN answered true** (D31). The numeric comparison
  primitives, `min:`, `max:`, `between:and:` and the element and key equality
  of `OrderedCollection`, `Bag` and `Dictionary` used protoCore's `compare`,
  a total order that reports a NaN as equal to every number:
  `Float nan = Float nan`, `Float nan = 1`, `Float nan <= 1` and
  `1 = Float nan` were true, `Float nan min: 1` answered NaN, a `Bag` holding
  only a NaN included 7, a `Dictionary` keyed by one NaN answered its value
  for another NaN, and `remove: 2.5` removed a NaN. They now use protoCore's
  `partialCompare`, which follows IEEE 754: a NaN is unordered with every
  number, so every ordering and `=` are false and `~=` is true; `min:` and
  `max:` answer the argument; these collections find a NaN only by identity.
  Comparisons of other numbers are unchanged. `Set` membership is protoCore's
  hashed membership and is not affected (see Known issues). Regression tests:
  `conformance/12-builtins/float-nan-comparison.st` and
  `conformance/09-collections/nan-elements.st`.
- **In the REPL, a block assigning a session variable declared a new block
  variable instead** (S10). After `s := 0.`, the input
  `#(1 2) do: [ :x | s := s + x ]` failed with
  `doesNotUnderstand: + (receiver: nil)` and `[ s := 7 ] value` left `s` at 0:
  the compiler turned an assignment inside a block into a block-local
  declaration whenever it was not at module scope, although a read of the
  same name resolved to the session global. In REPL mode an assignment in a
  block of module-level code to a name that no enclosing scope binds now
  updates the global; block temporaries and arguments still shadow it, and
  method bodies and scripts are unchanged. Regression test: new cases in
  `tests/cli/test_cli_repl.sh`.
- **An actor suspended in `wait` could resume before the awaited future
  settled** (S13). Any scheduler wakeup of a suspended actor resumed its
  method: a message sent to it while it waited, or one that arrived during
  the turn in which it suspended, made `wait` answer `nil`
  (`nil + 1` → `doesNotUnderstand:`). The unit test "F6 v3 E3: cooperative
  chain survives aggressive GC on 4 workers" failed intermittently for this
  reason (4 runs in 300 on this tree, 12 in 300 on the build before S4). A
  wakeup now resumes the method only when the awaited future has settled;
  otherwise the actor stays suspended, its queued messages wait for the
  method to finish, and the future's settlement reschedules it. Regression
  test: `conformance/10-actors/resume-waits-for-awaited-future.st`.

### Known issues

- The large-rope garbage-collector issue (K2) is fixed in protoCore; see
  [`KNOWN_ISSUES.md`](KNOWN_ISSUES.md).
- Blocks created in different iterations of a loop share the loop variable:
  after `1 to: 3 do: [ :i | bs add: [ i ] ]` every stored block answers 3,
  because a method or module activation keeps one captured-variable
  dictionary (D30, open). See `docs/STATUS.md`.
- A thread running an allocation-free loop (an inlined loop over
  SmallIntegers, a block loop, allocation-free recursion) never reaches a
  garbage-collector park point, so a requested collection waits for its loop
  to end (S3, open). A fix based on protoCore's park-only safepoint was
  reverted when protoCore withdrew that API. See `docs/STATUS.md`.
- Hashed collections do not agree on element equality (D32, open): a `Set`
  uses protoCore's hashed membership, so `1` and `1.0` are two elements and
  all NaNs are one; a `Dictionary` misses a key `1` looked up as `1.0`; a
  `Bag` compares with `=`. See `docs/STATUS.md`.

### Tests

- **CLI tests no longer fail intermittently on SIGPIPE** (S11).
  `tests/cli/test_cli_help.sh` failed about 4 runs in 1000 with
  `--help missing 'Usage:'` although the text was there: under
  `set -o pipefail`, `protost --help | grep -q …` let `grep` exit at the
  first match and close the pipe while `protost` was still writing, so
  `protost` died of SIGPIPE (exit 141) and the pipeline failed. Every CLI
  test script now captures a command's output before searching it
  (`out=$(…)` then `grep … <<< "$out"`), which also removes the latent
  `echo "$out" | grep -q` form (46 checks in 6 scripts) that can fail the same
  way once the output exceeds a pipe buffer.
- 753 → 833 `ctest` cases (352 conformance, 42 examples, 12 CLI, 427 unit).

## 0.3.0 — yieldable iteration (2026-05-23)

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

751 → 753: two new conformance tests — `tests/conformance/do_yielding.st`
for the `doYielding:` desugar, and `tests/conformance/do_yielding_actor.st`
for the race fix (3 sinks × driver-actor with `doYielding:` containing
`wait`, multi-worker stable).

## 0.2.0 — Performance pass (2026-05-23)

An optimisation pass on the actor dispatch path.
Headline: `mt100a` (the round-trip throughput benchmark) moves from
**~ 30 K msg/s** to **71.9 K msg/s** on the Ryzen 5 5500U notebook — a
**+143 %** improvement. The report projects, without measuring, about
135–150 K msg/s on a Ryzen 7 7700X / Intel i9-13900K.

Three concurrent-runtime bug fixes also landed alongside the
optimisations — each was a real correctness improvement uncovered by
the performance investigation, not a tuning knob.

### Throughput (best of 3, AMD Ryzen 5 5500U, 6 physical cores)

| benchmark | before (0.1.0) | after (0.2.0) | factor |
|---|---|---|---|
| `mt100k` w=1  | ~ 20.6 K msg/s | 36.6 K        | +78 %  |
| `mt100a` w=1  | ~ 29.6 K       | 68.5 K        | +131 % |
| `mt100a` w=2  | — (not optimal) | **71.9 K**    | peak |
| `mt100a` w=4  | regression       | 71.9 K (no regression) | fixed |
| `saturation_big` w=6 scaling | regression at w=8 | **3.88×** at w=6 | fixed |

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

Companion protoCore commits (`ea2c17f4`, `ed38a499`, `90aade34`):

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
built on the [protoCore](https://github.com/numaes/protoCore) kernel.

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
