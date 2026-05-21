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

- **Baseline:** 568/568 tests passing at commit `6add592`.
- **Last verified:** 2026-05-20 (every D-item below was probed against a fresh
  `cmake --build build` of `6add592`).
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

### Builtins / primitives
- [x] `SmallInteger` arithmetic & comparison (`+ - * /`, `< <= > >=`, `= ~=`)
- [x] `SmallInteger` predicates `isEven` / `isOdd` *(closed: C2)*
- [x] `Number` iteration helpers (`to:`, `to:by:`, `to:do:`, `to:by:do:`)
- [x] `Boolean` `ifTrue:`, `ifFalse:`
- [x] `String` / `Symbol` (`,`, `size`, `=`, `printNl`)
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
| D1 | **Negative integer/float literals do not lex.** A leading `-` is always binary minus, so `-5` and `-3.14` fail to parse as literals. Contradicts §2.4. | `./protost -e "-5"` → `expected primary expression, got BinaryOp` | Medium |
| D3 | **`doesNotUnderstand` is a hard failure, not a catchable `Error`.** An unknown selector raises a hard runtime error that `on: Error do:` cannot catch. §5.2 specifies a catchable `doesNotUnderstand:` send. | `./protost -e "[3 fooBar] on: Error do: [:e | 99]"` → `error: doesNotUnderstand: fooBar` (uncaught) | High |
| D5 | **Class-side methods are not isolated from instances.** A `ClassName class >> sel` method installs on the same prototype as instance methods, so an instance can also receive it. §4.7 specifies a class/instance split. | `Counter class >> startingAt: n …` then `(Counter new) startingAt: 5` → returns an object instead of `doesNotUnderstand` | High |
| D8 | **Dead-home non-local return is a hard error, not `BlockCannotReturn`.** A `^` in a block whose home method already returned raises a hard error rather than a catchable `BlockCannotReturn`. Contradicts §7.1. | `Maker >> makeBlock saved := [ ^ 99 ]. ^ self.` then invoke `saved value` after `makeBlock` returned → `error: non-local return: home method has already returned` | Medium |
| D13 | **`protost compile` not implemented.** The usage text advertises `protost compile script.st -o out.stbc`; the subcommand is rejected. (Behaviour has improved since §14 was written: it is now an explicit `Unknown option or mode: compile` error rather than silently falling through to be treated as a script path.) | `./protost compile /tmp/x.st -o /tmp/x.stbc` → `Unknown option or mode: compile` | Low |
| D15 | **`classVariableNames:` is parsed then silently discarded.** The clause is accepted but its contents are dropped — class variables do not exist. Silent acceptance of a no-op clause is a bug; see also D19 for the missing feature. | `Object subclass: #Counter instanceVariableNames: 'value' classVariableNames: 'Total'.` → parses, `Total` unusable, no diagnostic | Medium |
| D16 | **Nested literal arrays not parsed.** A `#( … )` literal admits only flat elements; a nested `#( … )` inside one fails to parse. Contradicts §2.9. | `./protost -e "#(1 #(2 3) 4)"` → `unexpected token in frozen array literal` | Medium |
| D18 | **Identity comparison `==` / `~~` unbound; `=` not universal.** `==` and `~~` lex and parse but are bound on no class. `=` is bound only on `SmallInteger` and `String`; `~=` only on `SmallInteger`. The design-spec pump example uses `state == #operating`, which does not work. | `./protost -e "3 == 3"` → `error: doesNotUnderstand: ==` | High |

---

## Not yet implemented

Planned features that are simply absent today. Each is tagged with the roadmap
track that owns it.

| Id | Feature | Track |
|----|---------|-------|
| D9 | **Richer control-flow selector set.** Only `ifTrue:` / `ifFalse:` are bound on `Boolean`. Missing: `ifTrue:ifFalse:`, `ifFalse:ifTrue:`, `and:`, `or:`, boolean `&` / `\|`, and the nil-test selectors `ifNil:`, `ifNotNil:`, `isNil`, `notNil`. A two-armed conditional must be two separate sends today. | Track 1 (language core) / Track 2 (`Boolean` & `UndefinedObject` protocol) |
| D10 | **No `Transcript`.** Smalltalk-80's standard output-stream object is not provided; `Transcript show:` / `cr` do not work. Use `printNl`. | Track 4 (standard library — streams / I/O) |
| D11 | **`Float` and mixed-mode arithmetic not bound.** Arithmetic and comparison primitives are bound on `SmallInteger` only. A `Float` arithmetic send is a `doesNotUnderstand`; float *literals* lex and parse, only the operations are missing. Mixed integer/float arithmetic is likewise absent. | Track 1 / Track 4 (numeric tower) |
| D14 | **REPL meta-commands limited to `:help` / `:quit`.** The design spec lists `:load`, `:reload`, `:edit`, `:time`, `:doc`; none are implemented. | Track 7 (onboarding / tooling) |
| D17 | **`thisContext` is reserved but inert.** It parses to its own node but the reflective context protocol is unbuilt; using it errors with `expression kind not yet supported`. | Track 3 (advanced object model / reflection) |
| D19 | **Class variables are not implemented.** The runtime feature behind D15: a per-class shared variable visible to all instances and class-side methods. D15 (the silent-discard parser bug) and D19 (the missing feature) are tracked separately so D15 can be fixed — by diagnosing or honouring the clause — independently of building D19. | Track 1 (language core) / Track 3 |
| D20 | **`LargeInteger` arithmetic not bound — no overflow promotion.** protoCore already provides arbitrary-precision integers and *transparently promotes* on overflow. protoST binds arithmetic on `SmallInteger` only, so a `SmallInteger` computation that overflows does **not** promote to `LargeInteger` — it has no defined arbitrary-precision path. The owner explicitly wants `LargeInteger` supported: `SmallInteger` arithmetic must promote transparently on overflow, mirroring protoCore. This is the integer half of the same numeric-tower gap as D11 (the float half). | Track 1 / Track 4 (numeric tower) |

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

---

*Maintainers: keep this file in sync with every change. A change that closes an
item must move it to *Closed items* with its commit SHA; a change that opens one
must add it to *Open bugs* or *Not yet implemented* with a fresh stable id.*
