# protoST — Status

**This document is the living delta between [`docs/LANGUAGE.md`](LANGUAGE.md)
(the *ideal* — what protoST is specified to be) and the implementation (the
*reality* — what the current build actually does).**

`LANGUAGE.md` describes the language as designed. This file records, item by
item, where the implementation has not caught up, where it deviates *on
purpose*, and what is genuinely broken. It is a tracker, not a reference.

**It must be updated with every change that opens or closes an item.** When a
bug is fixed, move it to *Closed items* with the fixing commit SHA. When a
"not yet implemented" feature lands, move it to *Implemented* and tick the
relevant checklist line. When a new divergence is discovered, give it a fresh
stable id and file it in the right bucket.

- **Test suite:** 832 `ctest` cases (351 conformance, 42 examples, 12 CLI,
  427 unit), counted with `ctest -N` on 2026-09-15; 832/832 pass with the
  D31 fix (one conformance case is an expected failure pinning D30); the S10
  fix adds cases to `cli_repl`. Before it: 830/830 with the S4 fix, also with `PROTOCORE_GC_MIN_BUDGET_CELLS=4096`
  (a protoCore variable since withdrawn), 826/826 with the D29
  fix, 807/807 with the D28 fix, 797/797 with the S6 fix, 793/793 with the
  D25, D26 and D27 fixes, and 786 cases before those.
  The commit that added the priority bands (`3efb31d`, 2026-06-15) reports
  785/785 passing. Earlier: 753/753 at 0.3.0; 751/751 at 0.2.0 and at
  0.1.0; 746/746 at the D22/D23 close; 702/702 at T5-a (`5be7af9`); 699/699
  at T3-c (`f5e7643`); 622/622 at the numeric-tower fix (`42c4dde`).
- **Last verified:** 2026-06-15 (actor priority bands, commit `3efb31d`;
  785/785 per that commit). Earlier: 2026-05-23 (**0.3.0 yieldable iteration** —
  `doYielding:` compiler-recognised selector lifts the
  no-`wait`-inside-`do:` limitation; race fix in `finishDrain`
  stale-wakeup respects suspended flag; multi_producer benchmark
  now correct end-to-end. See
  [CHANGELOG.md](../CHANGELOG.md#030--yieldable-iteration-2026-05-23)).
  Earlier: 2026-05-23 (0.2.0 performance pass — mt100a w=2 to
  ~ 71.9 K msg/s, saturation_big w=6 to 3.88× scaling).
- **Open bugs:** three (D30 and S3, Medium; D32, Low).
  Hard edges that are not language-design choices — one runtime per process,
  no `%` formatting — are recorded in [`KNOWN_ISSUES.md`](../KNOWN_ISSUES.md).
- **Id scheme:** `D1..D18` are carried over from `LANGUAGE.md` §14 and keep
  their original meaning. New divergences get new ids (`D19+`).

---

## Implemented

What works today, by area. Phase/track tags (`f1-complete` … `track11-complete`)
are noted where useful.

### Lexer / parser
- [x] Comments, identifiers, keyword tokens
- [x] Integer literals, float literals, character literals, string literals,
      symbol literals *(f1-complete)*
- [x] Flat array-literal syntax `#( … )`
- [x] Dynamic-array syntax `{ … }`
- [x] Operators, the `>>` method marker, cascades `;`
- [x] Class declarations (`subclass:` / `instanceVariableNames:`)
- [x] Method definitions (unary / binary / keyword selector patterns)
- [x] **Call-form method declarations** `Class >> name(p, k = default)`
      and **call-form sends** `recv name(p, k = v)` — protoCore convention,
      coexists with Smalltalk forms. Known restriction: a class cannot
      host both `>> bar` (unary) and `>> bar(...)` (call) on the same
      name. Actor receivers reject call-form sends in v1.
      *(closed: 2026-06-13)*
- [x] Message-precedence parsing (unary > binary > keyword)
- [x] **Chained assignment** `a := b := 0` *(closed: C1)*

### Object model
- [x] Everything is an object; prototype-based model
- [x] Defining a class; creating instances via `new`
- [x] Instance variables
- [x] `self` / `super` sends
- [x] Class-side methods *(defined; but see D5 — not isolated)*
- [x] `printString`
- [x] **Extensible classes from modules** — `subclass:` is a runtime message
      on any class object, so an imported module class can be subclassed in
      the importing program, methods overridden, and `super` used in an
      override to reuse the module's implementation *(Track 3, T3-a)*
- [x] **Multiple inheritance / mixins** — a class may be defined with several
      superclasses / mixins via the `uses:` clause
      (`Object subclass: #Foo uses: { MixinA. MixinB }`, also
      `subclass:instanceVariableNames:uses:` and the expression-receiver
      message form). Method/attribute lookup and `super` walk the parents
      depth-first, left-to-right — the primary superclass subtree first, then
      each mixin subtree in listed order; the diamond case resolves to the
      first in that order. Mixin instance variables work. *(Track 3, T3-b)*
- [x] **On-the-fly behaviour composition** — `aClass addBehavior: aMixin`
      (lower-level alias `addParent:`) composes a behaviour into a class at
      runtime with no recompilation. The class object and every instance
      created *after* the call respond to the mixin's methods. *(Track 3,
      T3-c)* — see D21 for the documented "future instances only" limitation.

### Blocks / closures
- [x] Block syntax, 0–4 argument blocks
- [x] Closures with variable capture *(f1-complete)*
- [x] `whileTrue:`, `to:do:`, `to:by:do:`

### Non-local return
- [x] `^expr` in a nested block returns from the enclosing method
      *(track1-complete)*

### Exceptions
- [x] Exception hierarchy (`Exception` / `Error` / `Warning`)
- [x] `signal`, `signal:`, `messageText`
- [x] `on:do:`, `on:do:on:do:`
- [x] Handler actions `return:`, `resume`, `resume:`, `retry`, `pass`
- [x] `ensure:`, `ifCurtailed:`
- [x] Native-exception translation (`ZeroDivide`, …)

### Collections
- [x] `Array`, `OrderedCollection`, `Interval`, `Set`, `Bag`, `Dictionary`,
      `Association`
- [x] Shared iteration protocol (`do:`, `collect:`, `select:`, `reject:`,
      `detect:`, `detect:ifNone:`, `inject:into:`, `do:separatedBy:`, `count:`,
      `anySatisfy:`, `allSatisfy:`, `,`, `asArray`, `size`, `isEmpty`,
      `notEmpty`, `species`)

### Actors / futures
- [x] Promoting an object to an actor; asynchronous message forwarding
- [x] `Future`: `wait`, `thenDo:`, `catch:`, `resolve:`, `rejectWith:`
- [x] **`Future new`** yields a usable first-class promise *(closed: C3)*
- [x] Cooperative suspension; parallel scheduler
- [x] **Lock-free actor mailbox + Future** — no per-actor or per-future mutex;
      the mailbox read-modify-write and the Future state machine run on
      protoCore's atomic attribute compare-and-swap (`setAttributeIfEqual`).
      A language built on protoCore carries no synchronisation locks of its
      own; only the DAP/debugger I/O locks remain (external-protocol
      coordination, outside protoCore's object model).
- [x] **`Atom`** — a shared mutable cell with optimistic-concurrency CAS:
      `Atom on:`, `value`, `value:ifCurrent:` (the raw compare-and-swap) and
      `swap:` (the read-modify-CAS retry loop). The agent/atom pair, atom
      side. Plus `Object>>setInstVar:from:to:` — the raw CAS exposed on any
      instance variable, for callers driving their own validate-and-retry.
- [x] **Actor priority bands (2026-06-15)** — three strict-priority queues
      backing the existing lock-free scheduler: `asActor` is Medium (the
      default, backward compatible), `asHighPriorityActor` lands in High,
      `asLowPriorityActor` in Low. Workers drain High → Medium → Low.
      Priority affects WHICH actor a worker picks up next; within-actor
      mailbox order is unchanged and the single-method invariant holds in
      every band. See `examples/actors/05_priority_bands.st`.

### Modules
- [x] File-to-module mapping; `Import from:`
- [x] protoCore UMD provider registration
- [x] **`Import from:` on every thread; each module's top level runs once** —
      imports work inside actor methods (and on any other thread running
      protoST code); concurrent importers of a module that is still loading
      wait for that load and receive the same module object; an import cycle
      raises `cyclic module import`. *(closed: S6)*
- [x] **Cross-language UMD interop, consumer side** — protoST `Import`s a
      module published by *any* registered `proto::ModuleProvider`, not just
      its own `provider:st`. `STRuntime::addModuleProviderToChain` wires a
      foreign provider's spec into the resolution chain; foreign objects
      dispatch protoST messages, carry through collections and blocks, and
      foreign immediates/strings need no conversion. Strategy:
      [`docs/INTEROP.md`](INTEROP.md). *(Track 5, T5-a)*

### REPL
- [x] `protost -i` read-eval-print loop with incomplete-input detection
- [x] Meta-commands `:help` / `:h`, `:quit` / `:q`, `Ctrl-D`

### Debugger
- [x] CLI debugger (`protost -d`)
- [x] DAP server (`protost --dap`) for VS Code

### Packaging
- [x] **CPack installers** — `cmake --build` followed by `cpack` produces
      native packages: `.deb` / `.rpm` / `.tar.gz` on Linux, a `.dmg` on
      macOS, an NSIS installer + `.zip` on Windows. The `protost` binary
      installs to `<prefix>/bin`; the stdlib `.st` modules to
      `<prefix>/share/protoST/lib` (the discovery in `STRuntime.cpp` probes an
      install-relative `<exe>/../share/protoST/lib`, so an installed binary
      resolves `Import from: …` with no `PROTOST_LIB`). The Debian package
      depends on `protocore` and the RPM `Requires: protoCore`. `cpack -G DEB`
      and `-G TGZ` verified on Linux; the macOS and Windows generators are
      configured per CPack's documented requirements but have not been
      verified. *(Track 10)*

### Documentation
- [x] **The dual-audience tutorial** — [`docs/TUTORIAL.md`](TUTORIAL.md) plus
      14 chapters under `docs/tutorial/`. Teaches protoST from the ground up
      for Python / JavaScript developers (with a constant Python/JS bridge) and
      catalogues every departure from Smalltalk-80 for Smalltalk programmers
      (Chapter 14). Every non-trivial code snippet was executed against the
      `protost` build. *(Track 8)*
- [x] **The comprehensive example set** — 40 complete, idiomatic, runnable
      protoST programs under `examples/` (42 registered as of 2026-09-15), grouped by theme (`basics/`,
      `blocks/`, `collections/`, `exceptions/`, `nonlocal/`, `actors/`,
      `stdlib/`, `modules/`, `programs/`). They span focused single-feature
      illustrations through genuine end-to-end programs — a recursive-descent
      calculator, an RPN interpreter, a Monte-Carlo pi estimate, a JSON data
      transform and two digital-twin simulations (a traffic intersection and
      the pump twin). Every example carries an `"EXPECT: …"` directive and is
      registered as a CTest case via `run_conformance.sh`, so the example tree
      is also a verified smoke layer (`ctest -R '^examples/'`, 40/40; 42
      registered as of 2026-09-15). The
      `examples/README.md` indexes the set. *(Track 9)*

### Benchmarks
- [x] **Performance benchmark suite** — `benchmarks/` carries two benchmark
      families and a harness. `benchmarks/comparable/*.st` is the protoPython
      core benchmark suite translated to idiomatic protoST (same algorithm,
      same N), so `benchmarks/run_benchmarks.py` can place protoST, CPython
      and (when a built `protopy` is found) protoPython side by side.
      `benchmarks/actors/*.st` are actor-model benchmarks with no Python
      counterpart — parallel speedup, cooperative-yield scaling (1000 actors
      on K=2 worker threads) and mailbox throughput. The harness does warmup +
      timed runs, reports the median and a geometric mean, and writes a dated
      report; `benchmarks/reports/2026-05-21-baseline.md` is the first one and
      `README.md` carries the headline numbers. *(Track 11)*

### Standard library
- [x] `lib/` infrastructure + the `Stream` module *(track4, T4-a)*
- [x] Mathematical protocol — `sqrt`, trig (`sin`/`cos`/`tan` + inverses),
      `ln`/`exp`/`log`/`log:`, rounding (`floor`/`ceiling`/`rounded`/
      `truncated`), `sign`/`squared`/`reciprocal`/`isZero`/`min:`/`max:`/
      `between:and:`/`asFloat`/`asInteger`/`even`/`odd`/`gcd:`/`lcm:`,
      exact `raisedTo:` and `factorial` (LargeInteger-safe), and class-side
      `Float` constants (`pi`/`e`/`infinity`/`nan`). Bootstrapped C++
      primitives on `Number` — always available, not a loadable module
      *(track4, T4-b)*
- [x] `lib/random.st` — the `Random` module: a seedable, deterministic
      pseudo-random generator (`seed:`, `new`, `next`, `nextInt:`,
      `between:and:`, `next:`). Pure protoST, a 32-bit LCG; no new
      primitives *(track4, T4-c)*
- [x] `lib/json.st` — the `JSON` module: `JSON parse:` (a JSON document →
      `Dictionary` / `Array` / `String` / number / `Boolean` / `nil`,
      recursive to any depth) and `JSON stringify:` (the inverse, with JSON
      string escaping). A hand-written recursive-descent scanner, pure
      protoST. Required two enabling additions, since the `String` protocol
      previously exposed no character access: minimal `String` accessors
      `at:` / `asInteger` and `Number>>asCharacter` (UTF-8-aware codepoint
      conversion), and registering `String` / `Boolean` as globals so the
      module can extend them with double-dispatch methods *(track4, T4-d)*
- [x] `lib/time.st` — the `Time` module: wall-clock access with a small
      timestamp / duration object model. `Time now` answers a `Timestamp`
      for the current instant; `Time millisecondsToRun:` benchmarks a block.
      `Timestamp` wraps epoch milliseconds (`asMilliseconds` / `asSeconds`,
      comparison, `Timestamp - Timestamp → Duration`,
      `Timestamp + Duration → Timestamp`); `Duration` wraps a millisecond
      span (`seconds:` / `milliseconds:` / `minutes:` constructors,
      arithmetic, comparison). Backed by two C++ clock primitives
      (`__currentMillis` from `system_clock`, `__monotonicMillis` from
      `steady_clock`) bootstrapped onto Object; no timezones, no calendar
      *(track4, T4-e)*

### Builtins / primitives
- [x] Numeric tower: `SmallInteger`, `LargeInteger` and `Float` arithmetic &
      comparison (`+ - * / // \\`, `< <= > >=`, `= ~=`, `negated`, `abs`),
      bound on the shared `Number` prototype, delegating to protoCore's own
      promoting / coercing arithmetic. Mixed-mode (`1 + 2.5`) coerces to
      `Float`; an integer overflowing the 54-bit `SmallInteger` range promotes
      transparently to an exact `LargeInteger` *(closed: D11, D20)*
- [x] `Number` predicates `isEven` / `isOdd` *(closed: C2)*
- [x] `Number` iteration helpers (`to:`, `to:by:`, `to:do:`, `to:by:do:`)
- [x] `Boolean` `ifTrue:`, `ifFalse:`
- [x] `String` / `Symbol` (`,`, `size`, `=`, `~=`, `at:`, `asInteger`,
      `printNl`); `Number>>asCharacter`. `size` and `at:` are codepoint-based
      (UTF-8-aware) *(`at:` / `asInteger` / `asCharacter` added in T4-d)*
- [x] `Block` evaluation and control-flow protocol

---

## Intentional deviations

protoST is **not** standard Smalltalk here, on purpose. These are design
decisions, not defects; they stay, documented with their rationale.

| Id | Deviation | Rationale |
|----|-----------|-----------|
| D2 | **Single `STRuntime` per process.** A second `STRuntime` in the same process can mis-resolve module imports. | The larger half of this — function-local `static` caches holding per-`ProtoSpace` interned symbols, which dangled into a freed space for every later runtime — is **fixed** (commit `8bf5b9c`, 0.1.0): symbols are now resolved fresh per call. The residual is process-global UMD state — protoCore's module provider and module cache outlive a `ProtoSpace`. The CLI always constructs exactly one runtime, so this never affects normal use. Tracked in `KNOWN_ISSUES.md` (K1). *(Classified intentional because protoST lives within the one-runtime-per-process contract; the residual closes when the UMD provider/cache become per-runtime.)* |
| D4 | **`new` does not auto-invoke `initialize`.** `ClassName new` returns a raw instance; the caller sends `initialize` explicitly. | A deliberate MVP semantics choice: `new` is the raw allocator and nothing more. Standard Smalltalk-80 defines `new` as `super new initialize`; protoST may adopt that later, but today the explicit two-step is the documented contract (§4.4). *(Intent is genuinely a judgement call — most Smalltalkers would expect auto-`initialize`. Left intentional because it is consistently documented and self-consistent; revisit if Track 1/3 decides to align with Smalltalk-80.)* |
| D7 | **`outer` is an alias of `pass`.** | MVP simplification of the handler protocol. True `outer` semantics (run the enclosing handler, then *return to the inner* handler) require resumable handler re-entry that is not built. `pass` (continue the search outward, do not return) is the shipped behaviour and covers the common case. Closing this is a strict-semantics refinement, not a correctness fix. |
| D12 | **No `main:` auto-invocation.** A script is simply its top-level forms run in order; the printed value is the last top-level statement. | Deliberate CLI semantics: protoST scripts are sequences of top-level forms, not programs with an entry point. Documented in §13. |
| D19 | **Class-variable mutation from instance-side methods is prohibited.** The `classVariableNames: '...'` clause is honoured: each declared name is installed on the class object as an `_iv_<name>` attribute initialised to nil, and any instance method can read it through the same prototype-chain attribute walk that resolves instance variables (so a class variable declared on `Base` is visible to instances of every subclass without re-declaration). Assigning to a class variable from an instance-side method is a compile-time error. | The natural store from an instance method would target `self` (the instance) and silently create a per-instance shadow rather than update the shared storage. Mutation therefore happens in a class-side method (`Class class >> sel`), where `self` is the class and the store hits the shared `_iv_<name>` slot. Subclass-side mutation also targets the subclass and shadows the superclass's class variable for that subclass and its instances — a prototype-language semantic, intentional and documented. |
| D21 | **`addBehavior:` affects future instances only.** `aClass addBehavior: aMixin` makes the class object and every instance created *after* the call respond to the mixin's methods; an instance created *before* the call does **not** gain them. | protoCore freezes an object's parent chain into its base cell at construction — `newChild` copies that frozen chain, and a later `addParent`/`setParents` on the class is invisible to the class's instances (verified by direct probing: even instances created *after* the mutation do not see it). `addBehavior:` therefore rebuilds the class with the mixin baked into the base chain and rebinds the global; this necessarily produces a new chain that only *future* `newChild` instances copy. Method *attributes* installed directly on a class (via `>>`) **are** seen by pre-existing instances — only new *parents* are not. Lifting D21 (making `newChild`-frozen chains observe later parent mutations) is a protoCore-level change, deliberately out of scope for this slice. Documented in `LANGUAGE.md` §4.12. *(Borderline: a real constraint born of a protoCore design choice; classified intentional because protoST exposes a genuinely useful capability — runtime behaviour composition with no recompilation — within the constraint rather than working around it. If protoCore ever makes parent chains live, this item simply closes and `addBehavior:` gains "all instances" semantics.)* |

---

## Open bugs

Behaviour that contradicts the language's own intent, its own examples, or
standard expectations. Each carries a minimal repro and a severity (High =
breaks documented behaviour or examples; Medium = surprising / blocks
idiomatic code; Low = narrow edge case).

| Id | Bug | Severity |
|----|-----|----------|
| S3 | **An allocation-free loop stalls a garbage collection.** protoCore starts a requested collection only after every running thread parks, and the dispatch loop has no park point: a thread running an inlined loop over SmallIntegers, a block loop (`cond whileTrue: body` with block variables) or allocation-free recursion holds every other thread in the stop-the-world handshake until its loop ends, or forever when the loop waits on a flag that a parked thread must set. It matters only while a collection is requested (with protoCore's original pacing: a heap limit, or `triggerGC()`). A fix that polled protoCore's park-only API at loop back-edges and frame entry (`a508a51`) was reverted (`d2a873f`) when protoCore withdrew that API; how the interpreter reaches a park point is open. A related proposal to submit the main thread's young generation at top-level statement boundaries, to shorten stop-the-world root collection (S12), was cancelled: root-collection cost is a protoCore question. | Medium |
| D32 | **Hashed collections do not agree on element equality.** A `Set` stores a protoCore `ProtoSet`, whose membership is by protoCore's hash: `1` and `1.0` are two elements (a Set holding `1` does not include `1.0`, although `1 = 1.0`), and all NaNs are one element (a Set holding one NaN includes another, although `Float nan = Float nan` is false). A `Dictionary` buckets keys by protoCore's hash, so a key `1` is not found as `1.0`. A `Bag` compares with `=` and finds both. `LANGUAGE.md` §9.6–9.8 do not state which equality decides membership. Found on 2026-09-15 while fixing D31; the same on the build before S4. Aligning the collections is a representation decision (options: `=` with a numeric-aware hash, protoCore hash equality everywhere, or documenting the difference). | Low |
| D30 | **Blocks created in different iterations of a loop share the loop variable.** `bs := OrderedCollection new. 1 to: 3 do: [ :i \| bs add: [ i ] ]` leaves three blocks that all answer `3`; §6.3 gives `1`, `2`, `3`, because a block argument belongs to one activation of its block. The same happens with `#(1 2 3) do:`, with a block temporary (`#(1 2 3) do: [ :x \| \| t \| t := x. bs add: [ t ] ]`), and inside a method. Cause: a method or module activation creates one captured-variable dictionary (`MAKE_CAPTURED`); a block reuses the dictionary stamped on it by `PUSH_BLOCK` and copies its captured arguments into it on entry, so each activation of the loop body overwrites the same entry. A fix needs a captured dictionary per block activation, chained to the enclosing one, with stores to an outer name reaching the dictionary that owns it: a runtime change to the capture model. Pinned by `conformance/06-blocks/closure-loop-iteration-binding.st` (XFAIL). Found on 2026-09-15 while testing D29; present before and after D29's fix. | Medium |

---

## Not yet implemented

Planned features that are simply absent today. Each is tagged with the roadmap
track that owns it.

| Id | Feature | Track |
|----|---------|-------|
| D10 | **No `Transcript`.** Smalltalk-80's standard output-stream object is not provided; `Transcript show:` / `cr` do not work. Use `printNl`. | Track 4 (standard library — streams / I/O) |
| D17 | **`thisContext` is reserved but inert.** It parses to its own node but the reflective context protocol is unbuilt; using it errors with `expression kind not yet supported`. | Track 3 (advanced object model / reflection) |

---

## Closed items

Resolved divergences. As bugs are fixed they move here with the fixing commit
SHA. Seeded with the Track 6 slice-3 fixes (commit `6e947c8`) and verified
during the 2026-05-20 audit.

| Id | What | Resolution | Commit |
|----|------|------------|--------|
| C1 | Chained assignment `a := b := 0` did not parse. | Assignment is now parsed as an expression, so it may be the RHS of another assignment (§3.6). Verified: `a := b := 7` yields 14. | `6e947c8` |
| C2 | `isEven` / `isOdd` were unbound on `SmallInteger`. | Bound as unary primitives on the `SmallInteger` prototype. Verified: `4 isEven` → `true`. | `6e947c8` |
| C3 | `Future new` produced a stateless, unusable object. | A dedicated `new` primitive on `futureProto` routes through `STRuntime::newFuture`, yielding a first-class promise that `resolve:` can settle and `wait` can return. Verified: `Future new` → `a Future`. | `6e947c8` |
| C4 | Conformance test "`wait` re-raises rejections" appeared to fail. | Root-caused as a malformed conformance test (a `^`-less `boom` method swallowed trailing top-level lines, §3.4), not an implementation bug. Test corrected; the `Future` machinery was already correct. | `6e947c8` |
| C5 | §10.1 implied a synchronous way to observe "this is an Actor" via `printString`. | Specification imprecision, not an implementation bug: the actor proxy forwards *every* message asynchronously, `printString` included — it is transparent by design. `LANGUAGE.md` §10.1 corrected. | `6e947c8` |
| D6 | Reported: a block could not declare a temp/argument with the same name as a captured variable of its enclosing method — the two would alias. | **Not reproducible on the current build.** Probed several variants (method temp + nested-block temp of the same name, with both referenced): the two variables stay distinct. `x := 100` in a method plus `\| x \|` in a nested block both used correctly yields 110, not 120. The flat-captured-dictionary aliasing described in §14 D6 does not occur today. Fixed incidentally before the 2026-05-20 audit; if a reproducer is found, reopen with a new id. | (incidental; pre-`6add592`) |
| D1 | Negative integer/float literals did not lex — a leading `-` was always binary minus. | The lexer now tracks whether the previous token *ends an operand*; a `-` immediately followed by a digit, in operand/primary position (or separated by whitespace, as in `#(-1 -2 -3)`), is lexed as the sign of a negative numeric literal. A `-` glued to an operand stays binary minus, so `a - 5`, `3 - 5` and `3 - -2` are unchanged. Verified: `-5`→-5, `-3.14` parses, `#(-1 -2 -3)`→3 elements. | `2544a45` |
| D9 | Only `ifTrue:` / `ifFalse:` were bound on `Boolean`; no nil-test protocol. | Bound on `Boolean`: `ifTrue:ifFalse:`, `ifFalse:ifTrue:`, `and:`, `or:` (lazy, block argument), `&`, `\|`, `xor:` (eager, boolean argument), `not`. Bound on `Object`: `isNil`, `notNil`, `ifNil:`, `ifNotNil:`, `ifNil:ifNotNil:` (the `ifNotNil:` block may take the receiver). Bound on `Block`: `whileFalse:`, `whileTrue`, `whileFalse`, `repeat`. `nil` answers `isNil`→true since `nilProto` descends from `objectProto`. | `2544a45` |
| D13 | `protost compile` was advertised in the usage text but not implemented. | The `compile` line was removed from the CLI usage/help text — the advertised surface now matches reality. Bytecode serialisation remains unimplemented (a separate feature). | `2544a45` |
| D15 | `classVariableNames:` was parsed then silently discarded. | A non-empty `classVariableNames:` clause now emits a clear compile-time diagnostic ("class variables are not yet supported — see D19"); an empty `classVariableNames: ''` stays a silent no-op. The real feature (class variables) remains tracked as D19. *(superseded 2026-06-13: D19 closed — the clause is now honoured. See the D19 row above and the entry below.)* | `2544a45` |
| D19 | Class variables were tracked as "not implemented" — the `classVariableNames:` clause was rejected at parse time. | The clause is now honoured: each declared name is installed on the class object as an `_iv_<name>` attribute initialised to nil at class-decl time (via the new `__initClassVars:` primitive on Object). Reads from any instance method walk the prototype chain via the same PUSH_INSTVAR path that resolves inst vars, so a class var declared on `Base` is visible from an instance of any subclass without re-declaration. Mutation is restricted to class-side methods: an instance-side assignment to a class var raises a compile-time error (the natural store would target `self` and silently create a per-instance shadow rather than update the shared storage). The remaining deviation, narrower than "not implemented", stays tracked in the intentional-deviations table as "class-variable mutation from instance-side methods is prohibited". Verified by `tests/conformance/04-object-model/class-variable-shared-storage.st` and five `[class-vars]` unit tests. | `6b3cf39` |
| D16 | Nested literal arrays (`#(1 #(2 3) 4)`) did not parse. | The `#( … )` literal-array parser was refactored to recurse: a nested `#( … )`, and per standard Smalltalk a bare `( … )` group, inside a literal array is a nested literal sub-array. Verified: `#(1 #(2 3) 4)`→3, `#(#(1 2) #(3 4))`→2. | `2544a45` |
| D18 | `==` / `~~` were bound on no class; `=` / `~=` were not universal. | `==` (identity) and `~~` (non-identity) are bound on `Object`, so every object understands them. `Object>>=` defaults to identity and `Object>>~=` to its negation; value-equality `=`/`~=` is bound on `SmallInteger`, `String` and `Boolean` (the `~=` on `String` was newly added). Symbols are interned, so `#foo == #foo` is true. The `~~` operator token was added to the lexer. Verified: `3 == 3`, `#foo == #foo`, `3 ~~ 4`, `3 = 3`, `'a' = 'a'`. | `2544a45` |
| D3 | `doesNotUnderstand` was a hard, uncatchable failure. | An unresolved selector now signals a catchable `MessageNotUnderstood` (a new subclass of `Error`) through the normal `signalInstance` handler-stack path. Root cause: the throw lived in the engine's own SEND dispatch, NOT inside a primitive, so it bypassed the EXC-d `translateNativeException` boundary (which wraps only the primitive call). The dispatch site now signals instead of throwing; with no handler the search still exhausts to `defaultAction` → `UnhandledSTException`, preserving the top-level/REPL abort. Verified: `[ 3 fooBar ] on: Error do: [:e| e messageText ]` → `doesNotUnderstand: fooBar`. The optional `doesNotUnderstand:` user hook was not implemented. | `c964f4e` |
| D5 | Class-side methods were not isolated from instances. | Class-side isolation via a marker (option b of the brief). `ClassName class >> sel` now installs through `__installClassMethod:as:`, which stamps the method wrapper with `__class_side__`; the engine's SEND dispatch hides a `__class_side__`-marked method when the receiver is an instance (does not own `__class_name__` as a direct attribute) — the send then falls through to `doesNotUnderstand`. Only this one direction is enforced: an instance-side method sent to a class object stays allowed, because the built-in class prototypes (`Array`, `Error`, …) deliberately double as both the class object and the instance-behaviour holder, and `__class_side__` is only ever stamped on USER class-side methods. The fuller separate-behaviours metamodel (option a) was judged too large to land safely in this slice. Verified: `(Counter startingAt: 10) value` → 10; `Counter new classOnly` → `doesNotUnderstand`. Incidental fix: `__setClassName:` now interns its key fresh per call (a stale per-`ProtoSpace` symbol made later runtimes' classes unrecognisable). | `c964f4e` |
| D11 | `Float` and mixed-mode arithmetic were not bound — a float arithmetic send was a `doesNotUnderstand`. | The numeric primitives (`+ - * / // \\`, `< <= > >=`, `= ~=`, `negated`, `abs`, `printString`) were rewritten to delegate to protoCore's own `ProtoObject` arithmetic — `add` / `subtract` / `multiply` / `divide` / `modulo` / `compare` / `negate` / `abs` — and rebound on the shared `Number` prototype, so `SmallInteger`, `LargeInteger` and `Float` all inherit one protocol. protoCore's arithmetic already coerces mixed Int/Float operands. The protoST way — minimal decoration over protoCore — so this was a rebinding + delegation change, not new arithmetic. The old primitives computed with raw C `long long` (`asLong`/`fromLong`), which could not touch a Float and silently wrapped on overflow. `Float` `printString` was also added (protoCore does not render numbers to strings; protoST formats them — a Float always shows a fractional part). Verified: `1.5 + 2.5` → `4.0`, `1 + 2.5` → `3.5`. | `42c4dde` |
| D20 | `LargeInteger` arithmetic was not bound — a `SmallInteger` computation that overflowed did not promote. | Closed by the same delegation as D11: protoCore's arithmetic *transparently promotes* an integer result that exceeds the 54-bit inline `SmallInteger` range to a heap arbitrary-precision `LargeInteger`. Because the protoST primitives now forward to it, an overflowing protoST computation stays exact with no extra work. `LargeInteger` `printString` extracts the exact decimal digits via repeated protoCore `divmod` by 10 (protoCore exposes no number→string conversion, and `asLong` would overflow). Verified: a `whileTrue:` loop computing `25!` yields the exact `15511210043330985984000000`. | `42c4dde` |
| D8 | Dead-home non-local return was a hard error. | A `^` in a block whose home method has already returned now signals a catchable `BlockCannotReturn` (a new subclass of `Error`). The engine keeps a thread-local registry of live `ExecutionEngine` instances; the block-frame `RETURN` opcode queries it (`homeFrameAlive`) — if no live engine on the thread holds the home activation, the home is genuinely dead and `BlockCannotReturn` is signalled THERE, while the handler stack is still intact. Signalling at the old outermost-`runWithArgs` escape site would have been too late: any `on:do:` on the path pops its handler as the `NonLocalReturn` unwinds through it. With no handler the run still aborts via `UnhandledSTException`. Verified: `[ blk value ] on: Error do: [:e| e messageText ]` → `non-local return: home method has already returned`; a live-home `^` is unaffected. | `c964f4e` |
| C6 | `Import from:` returned the un-unwrapped UMD *wrapper* (not the module) for any runtime other than the first one constructed in the process. | `prim_Import_from` unwraps the `exports` attribute of the wrapper that `getImportModule` returns. The `exports` key was interned in a function-local `static`, binding it to the FIRST runtime's `ProtoSpace`; symbols are interned per-space, so in a later runtime the stale key never matched the `exports` attribute protoCore stamps in that runtime's space — the unwrap missed and returned the wrapper, so the next message send saw `doesNotUnderstand`. The key is now resolved fresh from the live `ctx` every call. Surfaced by, and required for, the cross-language interop consumer path (a tri-runtime host constructs more than one runtime). Verified by `test_t5a_interop`. | `5be7af9` |
| D23 | Un-drained mailbox load deadlocked the actor scheduler non-deterministically — a sender firing many asynchronous sends at one actor without `wait`ing on the returned Futures hung indefinitely on a fraction of runs (~3 of 8). | Root cause (confirmed by gdb on a hung process): `STRuntime::workerLoop` acquired the scheduler mutex `schedMu` with a *plain* `std::unique_lock<std::mutex>` in its cv-wait section — the one `schedMu` acquirer that did **not** go through `gcSafeLock`. Every other acquirer (`registryAdd` / `registryRemove` / `schedule` / `drainOne`) takes `schedMu` GC-safely and, by design (`GcSafeMutex.h`), may park at a GC stop-the-world safepoint while owning it. When a worker hit the plain lock while `schedMu` was held by such a parked thread, it blocked in the kernel futex **off any safepoint while still counted as a running mutator** — so the STW quorum (`parkedThreads >= runningThreads`) could never be met, the holder's safepoint never returned, `schedMu` was never released, and the worker never unblocked: a three-way deadlock (worker ↔ schedMu holder ↔ GC). Fix: `workerLoop` now acquires `schedMu` via `gcSafeLock` and adopts it into the `unique_lock` (so `schedCv.wait_for` can still release / re-acquire it) — the same pattern `prim_Future_wait` uses. With this, *every* `schedMu` acquirer is GC-safe and the "a contender blocked on `schedMu` has already left the running set" invariant holds uniformly. Verified: the D23 repro completed 60/60 runs (was ~13/20); a 5,000-message un-drained variant 40/40; `PROTOST_WORKERS=1` 15/15. Regression tests added — `tests/cli/test_cli_actor_stress.sh` (25 repeated launches, fails fast on the first hang) and `conformance/10-actors/undrained-mailbox-load.st`. *(Superseded mechanism: the scheduler later became lock-free, and S4 removed `gcSafeLock` / `GcSafeMutex.h`; blocking waits use protoCore unmanaged regions.)* | `58d96a5` |
| D22 | Guard-clause `^` of a bare instance variable could be mis-compiled — a method that, in the guard-clause style, both *directly* assigned an instance variable and referenced it from a nested block errored with `>: argument is not a number` (or `doesNotUnderstand:` on whatever operator first touched the variable). | Root cause: the closure-capture analysis (`Compiler::analyseClosures`) had no knowledge of instance-variable names. A method body that directly assigned an instance variable made the analysis register that name in the method scope's `declared` set (its rule "at method scope, first-seen assignment is the declaration site"); a nested block referencing the same name then bubbled it up as a free variable, so `declared ∩ innerNeeds` *captured* it. The variable was boxed into a closure dict (`MAKE_CAPTURED`) that was never initialised from the object's real instance variable, so `PUSH_CAPTURED` read an uninitialised cell — a non-number. Fix: the scope walker (`ScopeWalker`) now carries the declaring class's instance-variable names — minus any method temp/arg that shadows one — and `freeVarsOf` excludes them. An instance variable is owned by `self`, never a free variable of a lexical scope, so it never enters any `innerNeeds`, never enters a captured set, and always compiles to `PUSH_INSTVAR` / `STORE_INSTVAR`. Verified: the D22 repro returns `50`; `balance := balance + 1` in such a method reads the live ivar (`100`→`101`); 742/742 tests green. Tutorial Chapter 14 §14.7 updated — the guard-clause form is now documented as working. | `6b640b7` |
| D14 | **REPL meta-commands limited to `:help` / `:quit`.** | The REPL gained `:load <path>` (execute a `.st` file into the live session — its definitions and variables persist exactly as if typed), `:reset` (discard all session state by sequentially destroying and reconstructing the `STRuntime` — no two runtimes are live at once, so D2's interning hazard does not apply), `:vars` / `:env` (list the user-defined globals — names absent from a builtin-globals snapshot taken at construction — each with a short value rendering), `:time <expr>` (evaluate `<expr>` and report wall-clock milliseconds alongside the result) and `:history` (recent input). `:help` lists every command. Meta-commands remain a REPL-only feature: `protost script.st` and `-e` are unaffected. Verified by the extended `tests/cli/test_cli_repl.sh`. | `12433e0` |
| D24 | Reported 2026-05-24: `Compiler::isCaptured` was described as walking past method-scope boundaries, so a method's reference to an instance variable whose name also appeared as a script-scope local captured by a script-level block would read `nil` (`doesNotUnderstand: size`). | **Not reproducible on the current build (2026-09-15).** The documented repro (a `Driver` class with instance variable `sinks`, and a script-scope `sinks` filled by `1 to: 3 do: [ :i \| sinks add: i ]`) returns `3`. Variants that force a real capture of the script-scope name — a non-inlined block stored in a variable that reads it, a block that assigns it, and a method that reads the instance variable inside a nested block — also return `3`, both through `d count` and through `(d asActor) count`. The fixing commit is not identified; if a reproducer is found, reopen with a new id. | (incidental; after `130e322`) |
| D25 | Intermittent heap corruption when several workers ran a method for the first time at the same moment: `malloc(): unaligned tcache chunk detected` / `tcache_thread_shutdown(): unaligned tcache chunk detected` aborts, SIGSEGV, or an instance variable reading as `nil`. `examples/programs/traffic_intersection.st` aborted in about 1 run of 100–130; a program sending 150 never-run methods to 8 actors failed 11–20 runs of 150. | Root cause: `BytecodeModule` filled three caches lazily with no synchronisation — interned selector/name symbols (`constSym`), `_iv_` instance-variable keys (`ivSymbol`) and parsed call-form descriptors (`callDescriptorSlot`), each a `std::vector` (re)allocated on first use. Two threads could both reallocate the vector; one freed the buffer the other was writing a symbol pointer into, corrupting glibc's tcache. Fix: one fixed-size table per module with an atomic entry per constant, allocated on first use and published with a compare-and-swap; a published table is never resized or freed while the module lives. Symbol entries are filled with plain atomic stores (racing threads store the same perpetual symbol). Call descriptors are parsed off to the side and published once by compare-and-swap, then never written. The cached local count is one atomic word. Hot path unchanged in cost (best of 5: `fib.st` 352→347 ms, `attr_lookup.st` 100→99 ms, `message_throughput.st` 32→32 ms). Verified by `tests/cli/test_cli_concurrent_first_call.sh` (40 launches of 200 fresh methods on 8 workers; failed 10/10 invocations before, 0/10 after) and two `[bytecode][concurrency]` unit tests (the symbol-cache test failed 20/20 before, 0/20 after). | `2cd74d3` |
| D26 | Memory corruption whenever an actor method contained a string literal longer than six bytes or a Float literal: SIGSEGV, hangs, or garbled error text such as `oNotUadertand`, on every run of such a program (a probe with a 54-byte literal sent 8000 times across 8 actors failed 60/60 runs; a Float-literal probe 30/30; a six-byte inline literal 0/30). | Root cause: `STRuntime::materialize`, which `PUSH_CONST` calls on whatever thread runs the method, allocated every literal on `rootCtx` — the main thread's context. A heap literal (a string longer than six bytes, a Float, a large integer) takes a cell from the context's thread-local free list, which is not synchronised, so worker threads and the main thread were handed the same cells. Fix: `materialize(ctx, module, index)` allocates on the calling thread's context; the engine passes its own. Nothing is cached across threads: each call builds a fresh immutable value that goes straight onto the frame's operand stack (a scanned GC root); the shared results are perpetual symbols, tagged immediates and the rooted bootstrap unset marker. The same pattern is fixed in `loadModuleFromFile`, which ran the module's top level on `rootCtx`; it now uses the caller's context through a new `runTopLevel(module, ctx)` overload, and the module containers it and `loadModule` update are guarded by a mutex held only for the container operations. (At the time that path was not reachable from actor methods: `Import from:` on a worker thread reported `module not found`, because the module provider's current-runtime pointer was thread-local and set only on the main thread. Fixed by S6.) Verified by `conformance/10-actors/literal-string-in-actor-method.st` and `literal-float-in-actor-method.st` (each 8 actors × 1000 rounds): 30/30 failing runs each before, 0/30 after. | `3bfe24d` |
| D27 | Latent memory-safety bug in the scheduler's lock-free ready queue (`ReadyStack`, a Treiber stack): a pop read `old->next` from a node another worker may already have popped and deleted (use-after-free), and its compare-and-swap was ABA-prone — the head node, then its successor, could be popped and a new node pushed at the first node's address, so the stale compare-and-swap installed a freed block as the head (lost actors, a later double free). The code comment claimed glibc malloc does not recycle freed pointers quickly; that is wrong — tcache is a per-thread LIFO and recycles immediately. Not observed in actor programs, but reproducible in isolation: against the extracted pre-fix algorithm, a concurrent push/pop conservation test crashed 10/10 runs and a deterministic interleaving test failed 10/10 (entry lost, then `free(): double free detected in tcache 2`). | `ReadyStack` moved to `src/runtime/ReadyStack.h` and rebuilt on a standard scheme: nodes live in a type-stable pool (chunks only ever added, popped nodes recycled through a lock-free free list, nothing freed while the stack lives), so a stale read always reads valid memory; nodes are named by 32-bit indices and both heads (stack and free list) are `(generation << 32) \| index` words bumped on every successful compare-and-swap, so a stale compare-and-swap fails even if the same index is back on top (a false match needs exactly 2^32 head updates inside one thread's load-to-CAS window); `next` links are atomics. A compile-time hook lets tests force the interleaving; it costs nothing in production. Cost: single-threaded micro-benchmark 24.5→26.7 ns per push+pop (depth 0), 29.6→32.6 ns (depth 16), under 0.02% of a message round trip; actor benchmarks unchanged within noise (interleaved best of 5/9, before→after: `message_throughput` 32→32 ms, `parallel_speedup` pool 452→413 ms, one worker 584→569 ms, `cooperative_yield` with 2 workers 1109→1113 ms). Nodes are retained at the high-water mark of queued entries (16 bytes each) until the runtime is destroyed. Verified by `tests/unit/test_ready_stack.cpp` (both tests 0/10 failures after). | `e169fd9` |
| S6 | `Import from:` inside an actor method answered `module not found: <name>` — on every run, for any module (a probe with 8 actors importing a module from a method failed 3/3 runs). The same held for any thread other than the one that constructed the runtime, including the DAP debuggee thread. | Root cause: `STModuleProvider` found its `STRuntime` through a thread-local pointer that only the runtime's constructor set, so on every other thread the provider declined the module and UMD reported it missing. Fix: a process-wide `ProtoSpace` → `STRuntime` association, registered by the constructor before the worker pool starts and removed by the destructor after the workers are joined; the provider resolves the runtime from the calling context's space, so no thread needs per-thread setup and a thread added later cannot miss it. Making imports reachable from several threads exposed a second defect: protoCore's `getImportModule` checks its cache, calls the provider and caches the result, without deduplicating concurrent loads, so eight actors importing a fresh module ran its top level eight times and received distinct module objects (8/8 in the probe with only the lookup fixed). The new `STRuntime::importModuleFile`, used by the provider and by `loadModule`, runs a module's top level once per runtime: the first importer records itself as the module's loader; later importers wait GC-safely (`enterGcBlocking` at the time; an unmanaged region since S4) for that load and receive the cached object, which is anchored in the live registry before it is published. A failed load is not cached and wakes the waiters, one of which retries. Because importers now wait, an import cycle would wait forever; before waiting, an importer follows the chain of loaders and the modules they wait for, and reaching itself raises `cyclic module import: <path>` (with the chain walk disabled, the two-actor cycle probe hung until killed in 3 of 3 runs). Remaining limit, documented in `LANGUAGE.md` §11.2: the check does not see Future waits, so a module's top level that waits on an actor message importing the same module deadlocks. Verified by four conformance tests in `11-modules/`, each failing 3/3 on a build of `e169fd9` (before S6): `import-from-actor-method.st` (`module not found` before; 5/5 passing after), `import-concurrent-runs-once.st` (`module not found` before; with the lookup fix alone the top level ran 8 times; 5/5 passing after), `import-cycle-errors.st` (before: unbounded recursion, `engine slot region exhausted`; 5/5 after) and `import-cycle-concurrent.st` (before: both actors caught `module not found`; 8/8 after). While verifying, an unrelated pre-existing bug on the import path was found and filed as D28. | `290173f` |
| D28 | An error raised by an imported module's top level reached the importer's handler twice. With `_raising_mod.st` signalling `Error signal: 'boom in module'`, `[ Import from: '_raising_mod' ] on: Error do: [ :e \| … ]` ran the handler once with `boom in module` and again with `exception unwind: no matching on:do: handler activation`, and `on:do:` answered the second value. `return:`, `retry` and `pass` in such a handler were affected the same way, and an import failing inside another module's top level reached the handler three times. On every thread; predates S6. | Root cause: `loadModuleFromFile` ran the module through `runTopLevel`, the outermost entry point for a script, whose catch clauses convert an `UnwindToHandler`, `RetrySignal`, `NonLocalReturn`, `ResumeSignal` or `PassSignal` that escapes the top level into a `std::runtime_error`. A module's top level runs nested inside the importer's frames — the importer's handlers are on the thread's handler stack — so the unwind thrown by the importer's handler targets a frame outside the module. The conversion replaced it with an error that `STModuleProvider` re-signalled as a new `Error`, and the importer's handler, enabled again, caught that too. Fix: the body of `runTopLevel` moved into a private `STRuntime::runModuleTopLevel` that lets control-flow signals propagate; `runTopLevel` wraps it with the conversions and `loadModuleFromFile` calls `runModuleTopLevel`. `importModuleFile`'s guard still drops the loading entry, so a failed load is not cached and the next import (or a `retry`) runs the top level again. The same path had a second pre-existing defect: `loadModuleFromFile` retained a module's `BytecodeModule` only after its top level completed, so a top level that declared a class and then raised freed the code of methods still reachable through the global class, and calling one crashed (SIGSEGV in `BytecodeModule::cachedLocalCount`). The module is now retained before its top level runs; a failed attempt keeps its bytecode until the runtime is destroyed. Verified by ten conformance tests in `11-modules/`. Nine failed 3/3 on pre-fix builds and pass 5/5 after: `import-error-handler-runs-once.st` (handler ran twice; `on:do:` answered `exception unwind: …`), `import-error-ensure-runs-once.st` (handler twice; the module's `ensure:` block ran once), `import-error-retries-load.st` (two failed imports ran the handler 4 times), `import-error-retry.st` (handler twice), `import-error-return.st` (handler twice), `import-error-pass.st` (inner and outer handler twice each), `import-error-nested.st` (handler 3 times), `import-error-from-actor-method.st` (handler twice) and `import-error-partial-module.st` (SIGSEGV). The tenth, `import-warning-resume.st`, guards `resume:` of a Warning signalled by a module's top level and passed before and after. 807/807 `ctest`. | `7e2fd7a` |
| D29 | The compiler crashed on block literals in inlined `to:do:` bodies. `1 to: 2 do: [ :i \| [ 1 ] value ]`, `1 to: 2 do: [ :i \| [ :e \| 2 ] ]`, `1 to: 2 do: [ :i \| [ 1 ] on: Error do: [ :e \| 2 ] ]` and the nested loop `s := 0. 1 to: 2 do: [ :i \| #(1 2) do: [ :x \| s := s + x ] ]`, at top level or in a method, died before the program ran with a segmentation fault or a floating-point exception in `Compiler::tryEmitInlinedControl`; other programs of the same shape compiled through freed memory without a visible failure. Found on 2026-09-15 while writing the D28 tests. | Root cause: the `to:do:` inlining in `tryEmitInlinedControl` took `auto& s = scopes_.back()` to bind the loop variable in that scope's `slots` around the body, and restored the binding through `s` afterwards. Compiling a block literal in either bound or in the body pushes a scope (`scopes_.emplace_back()` in `emitExpr`'s `Block` case). `scopes_` was a `std::vector`, so a reallocation left `s` dangling and the restore wrote into a freed `unordered_map` (the floating-point exception is its bucket arithmetic on freed memory). Fix: `scopes_` is a `std::deque`, whose `emplace_back` and `pop_back` never invalidate references to the other elements. The same inlining had a silent second defect: `isCaptured()` checks a scope's `capturedNames` before its `slots`, so when the enclosing scope captured a variable named like the loop variable, the body read that variable (`i := 5. b := [ i ]. s := 0. 1 to: 3 do: [ :i \| s := s + i ]` summed to 15, in modules and in methods). Such a loop is now compiled as a real block send. The other inlined forms (`ifTrue:`, `ifFalse:`, `ifTrue:ifFalse:`, `whileTrue:`, `whileFalse:`) hold no scope reference and were not affected; `to:by:do:` and `do:` are not inlined, and `timesRepeat:` is not bound. Verified by 18 conformance tests in `06-blocks/`, `07-non-local-return/` and `08-exceptions/`: 13 failed on the pre-fix build (the two shadowing tests answered `15 5`, the others crashed) and all pass after; the other 5 guard shapes that did not fail visibly before. `11-modules/import-error-retries-load.st` now retries in a loop (it crashed on the pre-fix build). While testing, a separate capture-model limitation was filed as D30. Loop benchmarks unchanged within noise (interleaved, best of 3, before→after): `int_sum_loop` 32→32 ms, `range_iterate` 47→50 ms (best of 5: 48→47), `fib` 373→377 ms, `list_append` 52→52 ms, `attr_lookup` 105→103 ms, `exception_latency` 972→1015 ms (best of 5: 1006→964), `str_concat` 16→16 ms. 826/826 `ctest`. | `66086e5` |
| S4 | A garbage-collector stop-the-world phase could start while a thread still ran, and the DAP debuggee shared the adapter thread's context. protoCore starts the phase once `parkedThreads >= runningThreads`. protoST's blocking waits (`GcSafeBlocking.h`: idle and paused workers, `Future>>wait` outside actors, a concurrent import's wait, the shutdown join) lowered `runningThreads`, while `sleep:`, the REPL, the DAP read and the debugger prompt used protoCore unmanaged regions, which raise `parkedThreads`; a thread counted both ways let the quorum complete early. The DAP debuggee ran on a plain `std::thread` sharing `rootCtx` with the adapter thread: the adapter's read counted that context as parked while the debuggee mutated the heap through it, and an `evaluate` while stopped ran a second engine on the same context, overwriting the stopped frame's slots — a DAP session with a breakpoint and evaluations while actors allocated failed with `doesNotUnderstand: do: (receiver: nil)` in 6 of 6 invocations. | Every blocking wait runs in `ProtoContext::UnmanagedScope` on the blocked thread's own context, opened before any `std::mutex`, and protoST no longer writes protoCore's thread counters (`git grep -nE "runningThreads\|parkedThreads\|gcCV" src/` is empty). `GcSafeBlocking.h`, `GcSafeMutex.h`, the dead `waitForSchedulerProgress` and the never-acquired per-actor `ActorLock` were removed. Young-generation submission moved to quiescent points: an idle worker calls `safepoint()` before sleeping, and a `Future>>wait` outside an actor only when exactly one engine is live on the thread (the old `exitGcBlocking` submitted inside nested primitives, where an unpinned collection iterator could be swept). The debuggee is a ProtoThread of the runtime's space with its own context; `enterSession`, `DebuggerFrontend::onStopped` and `evaluateExpression` take the halted thread's context, so a stop in an actor method prompts and evaluates on that worker. `drainOne`'s resume path pins the message and awaited futures. Verified by two `[s4]` unit tests that read the counters (before: `parkedThreads=0 runningThreads=1`, failing on every run; after: 2/3 idle, 3/3 while every thread blocks), `cli_dap_gc` (6/6 failing invocations before, passing after) and `cli_gc_shutdown_stress` (20 launches; passed before too, guards the join and nested-wait paths). 830/830 `ctest`, also with `PROTOCORE_GC_MIN_BUDGET_CELLS=4096`. The two CLI tests first lowered `PROTOCORE_GC_MIN_BUDGET_CELLS` to start collections; protoCore withdrew that variable with its allocation-budget pacing, so they now run with the default configuration and no longer force a collection (`cli_dap_gc` still fails 3/3 on the pre-S4 binary). How CLI tests should force collections is an open decision. Benchmarks (interleaved medians of 7, load average 10–13): `parallel_speedup` 568→423 ms (instructions 4.92G→4.42G), `cooperative_yield` with 2 workers 1411→1423 ms, `message_throughput` 46→49 ms. | `ef0e136` |
| D31 | Comparisons involving NaN answered true: `Float nan = Float nan`, `Float nan = 1`, `Float nan <= 1`, `Float nan >= 1` and `1 = Float nan` were true, `Float nan ~= Float nan` false, `Float nan min: 1` and `max: 1` answered NaN, `Float nan between: 0 and: 2` was true, a `Bag` holding only a NaN included 7 and counted one 1.5, a `Dictionary` keyed by one NaN answered its value for another NaN, and `anOrderedCollection remove: 2.5` removed a NaN. | Root cause: the numeric comparison primitives (`DEFCMP`, `prim_NumEq`, `prim_NumNe`), `min:` / `max:` / `between:and:` and the collection equality helpers (`indexOfEqual`, `countEqual`, `bucketIndexOfKey`, `Dictionary>>includes:`) used protoCore's `compare`, a total order that reports a pair involving a NaN as equal. They now use `ProtoObject::partialCompare` (IEEE 754: a NaN is unordered with everything, so every ordering and `=` are false and `~=` is true); `min:` / `max:` answer the argument when no ordering holds, and collections find a NaN only by identity. Other comparisons are unchanged. `Set` membership is hashed and unaffected (D32). `LANGUAGE.md` §12.2 documents the rule. Verified by `conformance/12-builtins/float-nan-comparison.st` (before `FTFTTFTFTTFTTTTT nan nan T`, after `FFFFFTFTFTFFFFFF 1 1 F`) and `conformance/09-collections/nan-elements.st` (before `a a 1 T removed 1`, after `a absent 0 F absent 2`). 832/832 `ctest`. | `7b62fe8` |
| S10 | In the REPL, a block assigning a session variable declared a new block variable instead: after `s := 0.`, `#(1 2) do: [ :x \| s := s + x ]` failed with `doesNotUnderstand: + (receiver: nil)` and `[ s := 7 ] value` left `s` at 0. Present on older builds as well. | Root cause: REPL mode compiles a top-level assignment to `STORE_GLOBAL` and never captures such names, but inside a block the assignment fell past the module-scope test to `declareLocal`, creating a block-local variable, while reads of the name resolved to the global (`Compiler::emitStatement` / `emitExpr`, `Assignment`). Fix: `Compiler::assignsReplGlobalInBlock` — in REPL mode, inside a block of module-level code, an assignment to a name that no enclosing scope binds as a temporary or argument emits `STORE_GLOBAL` (`LANGUAGE.md` §4.9, §13.1). Block temporaries and arguments still shadow the global; methods and scripts are unchanged. Verified by four new cases in `tests/cli/test_cli_repl.sh` (failing before, passing after). | `af81f67` |
| S11 | `tests/cli/test_cli_help.sh` failed intermittently (4 runs in 1000) with `--help missing 'Usage:'` although the help text contained it. | Root cause: under `set -o pipefail`, `protost --help \| grep -q` let `grep` exit at its first match and close the pipe while `protost` was still writing; `protost` died of SIGPIPE (exit 141) and the pipeline failed. Every check in the CLI test scripts that piped a command, or `echo "$out"`, into `grep` now captures the output first and searches it with a here-string (`test_cli_help`, `eval`, `venv`, `repl`, `dap`, `dap_session`, `debugger`, `dump_ast`). Verified: `test_cli_help.sh` 4/1000 failures before, 0/1000 after; 832/832 `ctest`. | `77dd2cc` |

---

*Maintainers: keep this file in sync with every change. A change that closes an
item must move it to *Closed items* with its commit SHA; a change that opens one
must add it to *Open bugs* or *Not yet implemented* with a fresh stable id.*
