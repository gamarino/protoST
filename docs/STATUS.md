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

- **Baseline:** 622/622 tests passing at commit `MNT-c` (597 carried over,
  plus 14 new `test_mnt_c_numbers` unit tests and 11 new numeric-tower
  conformance tests). Previous baseline 597/597 at `MNT-b2`.
- **Last verified:** 2026-05-21 (the MNT-c slice — D11, D20 — was fixed and the
  whole suite re-run three times green; the formerly-XFAIL conformance test for
  D11 was promoted to conforming, and `float-literal.st` was re-pinned to its
  spec-correct value).
- **Id scheme:** `D1..D18` are carried over from `LANGUAGE.md` §14 and keep
  their original meaning. New divergences get new ids (`D19+`).

---

## Implemented

What works today, by area. Phase/track tags (`f1-complete` … `track6-complete`)
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
- [x] Message-precedence parsing (unary > binary > keyword)
- [x] **Chained assignment** `a := b := 0` *(closed: C1)*

### Object model
- [x] Everything is an object; prototype-based model
- [x] Defining a class; creating instances via `new`
- [x] Instance variables
- [x] `self` / `super` sends
- [x] Class-side methods *(defined; but see D5 — not isolated)*
- [x] `printString`

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

### Modules
- [x] File-to-module mapping; `Import from:`
- [x] protoCore UMD provider registration

### REPL
- [x] `protost -i` read-eval-print loop with incomplete-input detection
- [x] Meta-commands `:help` / `:h`, `:quit` / `:q`, `Ctrl-D`

### Debugger
- [x] CLI debugger (`protost -d`)
- [x] DAP server (`protost --dap`) for VS Code

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
      `Float`; an integer overflowing the 56-bit `SmallInteger` range promotes
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
| D2 | **Single `STRuntime` per process.** A second `STRuntime` corrupts symbol interning. | protoCore's symbol caches are per-`ProtoSpace` C++ statics. Enforcing one runtime per process is the deliberate operating contract (the CLI always constructs exactly one); lifting it is a protoCore-level concern, not a protoST bug. Documented in §13.2. *(Borderline: it is a real constraint born of a protoCore implementation detail. It is classified intentional because protoST chooses to live within it rather than work around it; if protoCore ever makes the caches per-space-instance, this item simply closes.)* |
| D4 | **`new` does not auto-invoke `initialize`.** `ClassName new` returns a raw instance; the caller sends `initialize` explicitly. | A deliberate MVP semantics choice: `new` is the raw allocator and nothing more. Standard Smalltalk-80 defines `new` as `super new initialize`; protoST may adopt that later, but today the explicit two-step is the documented contract (§4.4). *(Intent is genuinely a judgement call — most Smalltalkers would expect auto-`initialize`. Left intentional because it is consistently documented and self-consistent; revisit if Track 1/3 decides to align with Smalltalk-80.)* |
| D7 | **`outer` is an alias of `pass`.** | MVP simplification of the handler protocol. True `outer` semantics (run the enclosing handler, then *return to the inner* handler) require resumable handler re-entry that is not built. `pass` (continue the search outward, do not return) is the shipped behaviour and covers the common case. Closing this is a strict-semantics refinement, not a correctness fix. |
| D12 | **No `main:` auto-invocation.** A script is simply its top-level forms run in order; the printed value is the last top-level statement. | Deliberate CLI semantics: protoST scripts are sequences of top-level forms, not programs with an entry point. Documented in §13. |

---

## Open bugs

Behaviour that contradicts the language's own intent, its own examples, or
standard expectations. **These are the work items for the bug-fix slices that
follow.** Each carries a minimal repro and a severity (High = breaks documented
behaviour or examples; Medium = surprising / blocks idiomatic code; Low =
narrow edge case).

| Id | Bug | Minimal repro | Severity |
|----|-----|---------------|----------|
| _(none — D3, D5, D8 closed in `MNT-b2`)_ | | | |

---

## Not yet implemented

Planned features that are simply absent today. Each is tagged with the roadmap
track that owns it.

| Id | Feature | Track |
|----|---------|-------|
| D10 | **No `Transcript`.** Smalltalk-80's standard output-stream object is not provided; `Transcript show:` / `cr` do not work. Use `printNl`. | Track 4 (standard library — streams / I/O) |
| D14 | **REPL meta-commands limited to `:help` / `:quit`.** The design spec lists `:load`, `:reload`, `:edit`, `:time`, `:doc`; none are implemented. | Track 7 (onboarding / tooling) |
| D17 | **`thisContext` is reserved but inert.** It parses to its own node but the reflective context protocol is unbuilt; using it errors with `expression kind not yet supported`. | Track 3 (advanced object model / reflection) |
| D19 | **Class variables are not implemented.** The runtime feature behind D15: a per-class shared variable visible to all instances and class-side methods. D15 (the silent-discard parser bug) and D19 (the missing feature) are tracked separately so D15 can be fixed — by diagnosing or honouring the clause — independently of building D19. | Track 1 (language core) / Track 3 |

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
| D1 | Negative integer/float literals did not lex — a leading `-` was always binary minus. | The lexer now tracks whether the previous token *ends an operand*; a `-` immediately followed by a digit, in operand/primary position (or separated by whitespace, as in `#(-1 -2 -3)`), is lexed as the sign of a negative numeric literal. A `-` glued to an operand stays binary minus, so `a - 5`, `3 - 5` and `3 - -2` are unchanged. Verified: `-5`→-5, `-3.14` parses, `#(-1 -2 -3)`→3 elements. | `MNT-b1` (this commit) |
| D9 | Only `ifTrue:` / `ifFalse:` were bound on `Boolean`; no nil-test protocol. | Bound on `Boolean`: `ifTrue:ifFalse:`, `ifFalse:ifTrue:`, `and:`, `or:` (lazy, block argument), `&`, `\|`, `xor:` (eager, boolean argument), `not`. Bound on `Object`: `isNil`, `notNil`, `ifNil:`, `ifNotNil:`, `ifNil:ifNotNil:` (the `ifNotNil:` block may take the receiver). Bound on `Block`: `whileFalse:`, `whileTrue`, `whileFalse`, `repeat`. `nil` answers `isNil`→true since `nilProto` descends from `objectProto`. | `MNT-b1` (this commit) |
| D13 | `protost compile` was advertised in the usage text but not implemented. | The `compile` line was removed from the CLI usage/help text — the advertised surface now matches reality. Bytecode serialisation remains unimplemented (a separate feature). | `MNT-b1` (this commit) |
| D15 | `classVariableNames:` was parsed then silently discarded. | A non-empty `classVariableNames:` clause now emits a clear compile-time diagnostic ("class variables are not yet supported — see D19"); an empty `classVariableNames: ''` stays a silent no-op. The real feature (class variables) remains tracked as D19. | `MNT-b1` (this commit) |
| D16 | Nested literal arrays (`#(1 #(2 3) 4)`) did not parse. | The `#( … )` literal-array parser was refactored to recurse: a nested `#( … )`, and per standard Smalltalk a bare `( … )` group, inside a literal array is a nested literal sub-array. Verified: `#(1 #(2 3) 4)`→3, `#(#(1 2) #(3 4))`→2. | `MNT-b1` (this commit) |
| D18 | `==` / `~~` were bound on no class; `=` / `~=` were not universal. | `==` (identity) and `~~` (non-identity) are bound on `Object`, so every object understands them. `Object>>=` defaults to identity and `Object>>~=` to its negation; value-equality `=`/`~=` is bound on `SmallInteger`, `String` and `Boolean` (the `~=` on `String` was newly added). Symbols are interned, so `#foo == #foo` is true. The `~~` operator token was added to the lexer. Verified: `3 == 3`, `#foo == #foo`, `3 ~~ 4`, `3 = 3`, `'a' = 'a'`. | `MNT-b1` |
| D3 | `doesNotUnderstand` was a hard, uncatchable failure. | An unresolved selector now signals a catchable `MessageNotUnderstood` (a new subclass of `Error`) through the normal `signalInstance` handler-stack path. Root cause: the throw lived in the engine's own SEND dispatch, NOT inside a primitive, so it bypassed the EXC-d `translateNativeException` boundary (which wraps only the primitive call). The dispatch site now signals instead of throwing; with no handler the search still exhausts to `defaultAction` → `UnhandledSTException`, preserving the top-level/REPL abort. Verified: `[ 3 fooBar ] on: Error do: [:e| e messageText ]` → `doesNotUnderstand: fooBar`. The optional `doesNotUnderstand:` user hook was not implemented. | `MNT-b2` (this commit) |
| D5 | Class-side methods were not isolated from instances. | Class-side isolation via a marker (option b of the brief). `ClassName class >> sel` now installs through `__installClassMethod:as:`, which stamps the method wrapper with `__class_side__`; the engine's SEND dispatch hides a `__class_side__`-marked method when the receiver is an instance (does not own `__class_name__` as a direct attribute) — the send then falls through to `doesNotUnderstand`. Only this one direction is enforced: an instance-side method sent to a class object stays allowed, because the built-in class prototypes (`Array`, `Error`, …) deliberately double as both the class object and the instance-behaviour holder, and `__class_side__` is only ever stamped on USER class-side methods. The fuller separate-behaviours metamodel (option a) was judged too large to land safely in this slice. Verified: `(Counter startingAt: 10) value` → 10; `Counter new classOnly` → `doesNotUnderstand`. Incidental fix: `__setClassName:` now interns its key fresh per call (a stale per-`ProtoSpace` symbol made later runtimes' classes unrecognisable). | `MNT-b2` (this commit) |
| D11 | `Float` and mixed-mode arithmetic were not bound — a float arithmetic send was a `doesNotUnderstand`. | The numeric primitives (`+ - * / // \\`, `< <= > >=`, `= ~=`, `negated`, `abs`, `printString`) were rewritten to delegate to protoCore's own `ProtoObject` arithmetic — `add` / `subtract` / `multiply` / `divide` / `modulo` / `compare` / `negate` / `abs` — and rebound on the shared `Number` prototype, so `SmallInteger`, `LargeInteger` and `Float` all inherit one protocol. protoCore's arithmetic already coerces mixed Int/Float operands. The protoST way — minimal decoration over protoCore — so this was a rebinding + delegation change, not new arithmetic. The old primitives computed with raw C `long long` (`asLong`/`fromLong`), which could not touch a Float and silently wrapped on overflow. `Float` `printString` was also added (protoCore does not render numbers to strings; protoST formats them — a Float always shows a fractional part). Verified: `1.5 + 2.5` → `4.0`, `1 + 2.5` → `3.5`. | `MNT-c` (this commit) |
| D20 | `LargeInteger` arithmetic was not bound — a `SmallInteger` computation that overflowed did not promote. | Closed by the same delegation as D11: protoCore's arithmetic *transparently promotes* an integer result that exceeds the 56-bit inline `SmallInteger` range to a heap arbitrary-precision `LargeInteger`. Because the protoST primitives now forward to it, an overflowing protoST computation stays exact with no extra work. `LargeInteger` `printString` extracts the exact decimal digits via repeated protoCore `divmod` by 10 (protoCore exposes no number→string conversion, and `asLong` would overflow). Verified: a `whileTrue:` loop computing `25!` yields the exact `15511210043330985984000000`. | `MNT-c` (this commit) |
| D8 | Dead-home non-local return was a hard error. | A `^` in a block whose home method has already returned now signals a catchable `BlockCannotReturn` (a new subclass of `Error`). The engine keeps a thread-local registry of live `ExecutionEngine` instances; the block-frame `RETURN` opcode queries it (`homeFrameAlive`) — if no live engine on the thread holds the home activation, the home is genuinely dead and `BlockCannotReturn` is signalled THERE, while the handler stack is still intact. Signalling at the old outermost-`runWithArgs` escape site would have been too late: any `on:do:` on the path pops its handler as the `NonLocalReturn` unwinds through it. With no handler the run still aborts via `UnhandledSTException`. Verified: `[ blk value ] on: Error do: [:e| e messageText ]` → `non-local return: home method has already returned`; a live-home `^` is unaffected. | `MNT-b2` (this commit) |

---

*Maintainers: keep this file in sync with every change. A change that closes an
item must move it to *Closed items* with its commit SHA; a change that opens one
must add it to *Open bugs* or *Not yet implemented* with a fresh stable id.*
