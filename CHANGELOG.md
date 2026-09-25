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

### Collections

- **`Set` and `Dictionary` share one mechanism: protoCore's `ProtoMap` and its
  hashed-collection helper** (Track S). A `Set` used to store a protoCore
  `ProtoSet`, keyed by protoCore's hash with no collision handling; a
  `Dictionary` used to store a hand-rolled hash-to-bucket `ProtoSparseList`.
  Both now store a `ProtoMap` — a `Set` being the map that holds each element
  under itself — reached only through `hashedPut` / `hashedGet` /
  `hashedRemove` / `hashedForEach`, with protoST supplying the equality and the
  hash. One hash, one equality, one collision policy, shared with protoScala
  and protoClojure.
- **Hashed collections decide membership with `=`** — closes **D32**, which had
  been open on a language decision nobody had taken. A `Set` holding `1` now
  includes `1.0`, a `Dictionary` keyed by `1` answers for `1.0`, and `Bag` is
  unchanged because that is already what it did. A NaN is found only by
  identity, which is what §12.2's `Float nan = Float nan` being false requires,
  so a `Set` holding one NaN no longer includes a different NaN object — the
  one behaviour change that is not a strict widening. `LANGUAGE.md` §9.6-9.8
  now state the rule, and also state that iteration order is unspecified; the
  decision, its argument and the cost of reversing it are in `docs/STATUS.md`,
  marked `[agent, pending review]`.

### Actors

- **Actor mailboxes are protoCore `ProtoMPSCQueue`s** (Track S). A send used
  to be a compare-and-swap retry loop that rebuilt the actor's whole mailbox
  `ProtoList` and republished it under `__mailbox__`; a pop rebuilt it again.
  The mailbox is now a lock-free multi-producer / single-consumer queue whose
  contents the garbage collector traces, the queue object's identity never
  changes, and a send is one `push` — O(1), one cell, no retry. A turn takes a
  whole batch with `takeAll` and parks anything it has not processed in a new
  `__pending__` attribute, which is read ahead of the queue on the next turn,
  so per-actor FIFO order is unchanged and a batch interrupted by a
  `FutureYield` is neither lost nor reordered.

  The old representation was quadratic in the depth of an undrained mailbox,
  in time and in memory. Measured on this machine (`benchmarks/reports/2026-09-24-mailbox-protompscqueue.md`): a single-producer send falls from
  1256 ns to 788 ns (and from 63.6 us to 3.5 us with `PROTOST_WORKERS=1`,
  where producers and sink share one thread), and the drain of a 2000-message
  backlog from 1.585 s to 4.1 ms; 200 000 undrained messages, which the old
  mailbox could not reach at all — 10 000 was already killed by the OOM reaper
  at over 10 GB resident — now enqueue in 510 ms and drain in 1.75 s.

  Requires protoCore 2.1.0, so protoST's version floor moves from 2.0 to 2.1.
- **Three priority bands** (commit `3efb31d`). `asHighPriorityActor` and
  `asLowPriorityActor` join `asActor` (Medium, the default). Workers drain
  strictly by priority — High, then Medium, then Low — with the
  single-method invariant unchanged in every band. There is no starvation
  guard. See `docs/tutorial/10-actors-and-futures.md` §10.11 and
  `examples/actors/05_priority_bands.st`.

### Interop

- **`provider:st` serves a caller in another runtime's `ProtoSpace`** (commit
  `e82682b`, S17). A cross-runtime import of a protoST module now resolves:
  protoScala's `import st.<module>` loads the module, its members bind by name,
  and the values are the protoST objects themselves — the same cell address and
  the same `getHash` read from either runtime, printed by
  `tests/unit/test_cross_runtime_provider.cpp`.

  `ModuleProvider::tryLoad(path, ctx)` receives the **caller's** context, and
  `STModuleProvider` resolved its runtime from `ctx->space` — a space protoST
  does not own when the caller is a co-resident runtime, so the lookup missed and
  a module that was there was reported as absent. The fix is in the provider and
  **protoCore is unchanged**: a `ModuleProvider` is an object with its own state,
  so it takes its runtime from that state (`soleSTRuntime()`) and uses `ctx` only
  to allocate the result in the caller's context.

  Two consequences worth knowing if you write a provider. The load runs in
  protoST's **own** space, because running a module's top level on a foreign
  context interns its literals in the foreign symbol table and leaves protoST's
  own later lookups missing. And the module **namespace** is rebuilt with keys
  interned in the **caller's** space, because an attribute key is an interned
  symbol's address and protoCore interns per `ProtoSpace`; only the mapping is
  rebuilt, never the values. protoCore embeds a short string in the pointer word,
  so a 5-byte member name matched across spaces by accident and a 7-byte one
  missed silently.

  `STRuntime::isOwnerThread()` is new. A cross-runtime load runs on protoST's
  root context, which only the constructing thread may allocate on (D26), so an
  import from any other thread is refused with a message rather than raced; a
  process holding two `STRuntime`s is refused as ambiguous. What this does **not**
  deliver — a foreign runtime *calling* a protoST method, imports from more than
  one thread, more than one runtime, a namespace that changes after import — is
  listed in [`docs/INTEROP.md`](docs/INTEROP.md) §4.2.

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

- **An actor mailbox batch could be collected in the middle of the turn that
  was processing it** (S16). `MailboxCursor::adopt` re-pinned the batch with
  `pin_.reset(new TransientPin(...))` on a `std::unique_ptr`, and
  `std::unique_ptr::reset` builds the replacement before destroying what it
  replaces — so a turn that adopted twice (a `__pending__` tail then a queue
  batch, or two non-empty `takeAll`s) released a scratch pin slot above one
  still in use, and the next `TransientPin` on that thread overwrote the live
  one. The batch then had no GC root at all: a collection freed the messages
  the turn had not reached and `next()` read a freed cell, segfaulting about
  two runs in five. The cursor now holds one pin for its lifetime and
  re-points it with `TransientPin::reset`. Introduced by the mailbox migration
  and invisible until S15 was fixed — a runtime that reclaims nothing cannot
  expose a lost root. Found by `cli_actor_payload_gc` on its first run.
- **protoST reclaimed nothing** (S15). A collection cycle
  never returned a cell to the heap: measured on the pre-fix build, four runs
  of a 20,000-iteration allocating loop left 2,748,398 cells in the root
  context's young generation, cycles ran and every one reclaimed exactly 0,
  and the heap grew monotonically. protoCore chains every cell a context
  allocates onto that context's young generation and treats the chain as a GC
  root until the context submits it — which happens when the context is
  destroyed, or from `ProtoContext::safepoint()` past a per-context
  threshold. protoST creates no `ProtoContext` of its own (a program runs on
  the runtime's root context, an actor turn on its worker's) and the
  interpreter called `safepoint()` nowhere at all, so every cell a program
  ever allocated stayed live by definition. `ExecutionEngine::gcSafepoint`
  now calls it at the loop back-edge and at engine entry, the two points
  where every live object is in a traced slot rather than a C++ local. After
  the fix the same workload holds the heap flat at 1,048,576 cells and
  reclaims about 730,000 cells per cycle, against a heap that grew to
  3,866,624 cells and reclaimed nothing before it. The same call is also
  protoCore's park point, so it very probably closes **S3** too — but that
  could not be proved, because no protoST loop is actually allocation-free
  (every shape measured allocates about three cells per iteration), so S3
  stays open. Cost, worst case, on a 20,000,000-iteration integer loop that
  does nothing but add: +2.7 % instructions, +4 % cycles.
  `PROTOST_NO_GC_SAFEPOINT=1` disables the hook. Regression tests: one `[gc]`
  unit case, `cli_gc_reclaims` (which also requires the hook-disabled run to
  fail) and `cli_actor_payload_gc`.
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

### Build

- **protoCore 2.0.0** (soname `libprotoCore.so.2`). protoST builds and passes
  its whole suite against it unchanged — no source change was needed. The ABI
  bump means every protoST build directory must be recreated from clean; a
  stale object now fails to load with a missing-soname error instead of
  linking against an incompatible layout. Of the four behaviour changes
  2.0.0 lists for embedders, only one is observable here, and only in
  documentation: see "Documentation" below. protoST calls no `setParents` and
  no `isInstanceOf`; its single `hasParent` call (`HandlerStack`, exception
  guard matching) keeps its semantics and is now allocation-free.
- **A fresh configure could link a stale protoCore** (S14). `CMakeLists.txt`
  searched `../protoCore/build` before `../protoCore/build_release`, so a
  leftover `build/` from an older checkout shadowed the library the ecosystem
  rebuilds (here a June build was picked over the current one). The search
  order is now `build_release`, `build`, `build_check`, as in protoClojure, and
  the not-found message suggests `build_release`. The README documents the
  order and how to override it.

### Documentation

- **The `addBehavior:` rationale claimed something protoCore 2.0.0 makes
  false.** `docs/STATUS.md` D21, `docs/LANGUAGE.md` §4.12, the tutorial
  (§11.3, §14.4), `src/primitives/object_prims.cpp` and
  `tests/unit/test_t3c_addbehavior.cpp` all stated that a direct
  `addParent`/`setParents` on a live class is invisible to **all** of that
  class's instances, past *and* future — "verified by direct probing" under
  protoCore 1.x, where `newChild` copied the prototype's birth-state chain.
  protoCore 2.0.0 copies the prototype's *current* chain, so instances created
  **after** such a mutation do see it; only instances that already exist do
  not. Re-probed directly against 2.0.0 and corrected everywhere. **D21 itself
  is unchanged** — its subject is pre-existing instances, and `addBehavior:`
  still has "future instances only" semantics — but the rebuild is no longer
  the only route to those semantics, which is now recorded where the rebuild
  is justified.

### Known issues

- The large-rope garbage-collector issue (K2) is fixed in protoCore; see
  [`KNOWN_ISSUES.md`](KNOWN_ISSUES.md).
- Blocks created in different iterations of a loop share the loop variable:
  after `1 to: 3 do: [ :i | bs add: [ i ] ]` every stored block answers 3,
  because a method or module activation keeps one captured-variable
  dictionary (D30, open). See `docs/STATUS.md`.
- A thread running an allocation-free loop never reaches a garbage-collector
  park point, so a requested collection waits for its loop to end (S3, open).
  The S15 fix adds `ProtoContext::safepoint()` at every loop back-edge, which
  is that park point, but the close is unproved: no protoST loop allocates
  nothing, so S3 cannot be exhibited. See `docs/STATUS.md`.
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
