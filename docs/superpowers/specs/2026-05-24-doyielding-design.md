# `doYielding:` — compiler-desugared yieldable iteration

**Status:** design, not yet implemented.
**Target:** unblock the multi-producer benchmark pattern documented in
`2026-05-23-multiproducer-blocker.md`. Lifting the yieldable-do:
limitation moves protoST from a producer-bounded 71.9 K msg/s ceiling
to an estimated 430 K msg/s on this 5500U notebook (~ 2 M msg/s on
modern desktop), per the projection in
`benchmarks/reports/2026-05-23-performance.md`.

## The problem (one paragraph)

A driver actor that fan-outs SENDs to N sinks and then waits on each
future is the natural multi-producer shape:

```smalltalk
Driver >> sendBatchToSinks
  | futures |
  futures := OrderedCollection new.
  sinks do: [ :s | futures add: (s ping) ].
  futures do: [ :f | f wait ].         "← f wait yields cooperatively"
  ^ self.
```

`do:` is a C++ primitive that calls `invokeBlock` per element, which
spins up a recursive `ExecutionEngine` on the C++ stack. The inner
`f wait` throws `FutureYield`; the inner engine's `runLoop` catches,
snapshots its frames, rethrows; the throw propagates through
`invokeBlock` and back to the OUTER engine's dispatch where `runLoop`
catches `FutureYield` AGAIN. The 2026-05-23 coalesce-snapshot fix
(commit `be160ea`) preserves the inner block frame in the snapshot,
but the C++ iteration STATE (which element are we on?) is gone —
unwound with the C++ stack. After resume, `do:` is dead; only the
first element ran.

That makes patterns like the one above unwriteable. The user is
forced into manual unrolling (`f1 wait. f2 wait. ...`) or recursion
on a tail-call-less engine (~ stack overflow at modest collection
sizes). Both ugly.

## Why compiler-desugar (vs the alternatives)

Three approaches were on the table after the 2026-05-23 investigation:

| approach | LOC | touches | risk |
|---|---|---|---|
| **Smalltalk recursion override + stdlib auto-load** | 200-400 | `lib/init.st` + runtime preload | bounded by frame stack capacity (~ 100-160 elements before overflow) |
| **Compiler desugar — THIS PROPOSAL** | 500-800 | parser/AST + compiler emit + 1 spec test file | **medium** — scope confined to frontend, no runtime risk |
| **Iterator-continuation frame in the engine** | 800-1500 | `Frame` struct + `RETURN_TOP` + `prim_*_do` primitives + snapshot serialisation | high — touches runtime core + GC-snapshot path |

The compiler-desugar approach wins because:

- **All changes confined to `src/frontend/` and bytecode emit.** Runtime,
  GC, snapshot/restore, and the actor scheduler are untouched. Any
  bug introduced cannot break a non-`doYielding:` callsite.
- **The runtime cost is zero** when the new form is not used. Existing
  `do:` callers see no change. Existing tests cannot regress.
- **Yieldability falls out for free** — the desugared loop uses
  `at:` (a primitive that doesn't take a block, so never yields) and
  `value:` (which already uses the engine's inline block fast-path,
  yieldable by construction).
- **No new mechanism to maintain.** The bytecode emitted is identical
  in shape to what the user could write by hand with `1 to: n do:` if
  that were yieldable. We're closing a gap, not adding architecture.

The trade-off: it only works for receivers that respond to `at:` and
`size` — SequenceableCollections. Set, Dictionary, and Bag cannot
be desugared this way because they have no positional access. Hence
the new selector (see next section) rather than overloading `do:`.

## API: a new selector `doYielding:`

We do NOT modify the semantics of `do:`. `do:` remains the polymorphic
collection-protocol selector that every iterable answers to. We
introduce a parallel selector `doYielding:` that is **compiler-
recognised** and only valid on receivers that respond to `at:` + `size`.

```smalltalk
"NEW yieldable form — for SequenceableCollections (Array, OrderedCollection,
 Interval, String)."
sinks doYielding: [ :s | (s ping) wait ].

"EXISTING form — works on every collection, but the block must NOT yield
 from within for unbounded-iteration receivers (Set, Dictionary, Bag)."
aSet do: [ :x | x printNl ].
```

The recognition is purely lexical: when the parser sees a binary or
keyword send whose selector is exactly `doYielding:` AND there is
exactly one argument AND that argument is a literal block with exactly
one parameter, the compiler emits the desugared form. Any other shape
(stored block, non-literal block, wrong arity) falls back to a normal
`SEND_KEYWORD` and produces a `doesNotUnderstand: doYielding:` at
runtime (we deliberately do NOT bind a primitive on the prototype, so
the falsely-shaped use surfaces immediately and loudly).

### Open question on naming

`doYielding:` is the working name. Alternatives considered:

- `doIndexed:` — accurate but doesn't communicate the yield property.
- `doSafe:` — meaningless overstatement.
- `eachDo:` — convention but conflicts with Pharo nothing.
- `do:awaiting:` — keyword arg syntactic mismatch.

`doYielding:` reads honestly: "iterate in a form that allows the block
to yield mid-iteration." Recommendation: keep `doYielding:`.

## The bytecode emitted

Source:

```smalltalk
coll doYielding: [ :elem | <body> ]
```

Desugared bytecode (pseudo-assembly — actual opcodes per
`src/runtime/Opcodes.h`):

```
;; Setup: compute and store the iteration bounds.
;;   (size is read once — protoST's collections are immutable
;;    at their value level, so the size is stable through the loop.)
PUSH coll                  ; the receiver
SEND_UNARY size            ; → n on stack
STORE_LOCAL n              ; local slot reserved by compiler
PUSH_INT 1
STORE_LOCAL i

LOOP_TEST:
PUSH_LOCAL i
PUSH_LOCAL n
SEND_BINARY <=             ; → bool on stack
JUMP_IF_FALSE LOOP_END     ; if i > n, exit

;; Body: invoke the block with the element.
PUSH coll                  ; receiver (deduplicated below)
PUSH_LOCAL i
SEND_KEYWORD at:           ; → element on stack
PUSH_BLOCK <block-module>  ; the literal block
SEND_KEYWORD value:        ; ← yieldable! engine inline frame push
POP                        ; discard block return value

;; Increment.
PUSH_LOCAL i
PUSH_INT 1
SEND_BINARY +
STORE_LOCAL i
JUMP LOOP_TEST

LOOP_END:
PUSH coll                  ; do: return value = the receiver
```

Every opcode here exists today. No new opcode required. The block's
`value:` send hits the existing block-direct fast path at
`ExecutionEngine.cpp:911` ("F6 v3 A2: direct block invocation fast-
path… pushes a Block frame on the current engine") — which is
precisely the path documented to be yieldable.

When the body of the block does `(...) wait`, the engine catches
`FutureYield`, the snapshot covers EVERY frame in this engine's
`frames_` — including the enclosing method frame, the block frame
holding the loop locals (`i`, `n`), and the inner block frame.
Resume restores everything; the next dispatched opcode is the one
right after `wait`. The next iteration is reached by the normal
`STORE_LOCAL i / JUMP LOOP_TEST` sequence already on the bytecode.

### Frame budget check

The new form allocates exactly **one** new method-frame's worth of
locals: `i` and `n`. Plus the existing operand stack. Plus the block
frame on every iteration (popped at iter end). No recursion, no
nested engine. Frame budget on snapshot/restore is bounded — never
grows with N.

## Implementation steps (compilable into a plan)

The work is in `src/frontend/`. Read `src/frontend/Parser.h`,
`src/frontend/Parser.cpp`, and `src/frontend/Compiler.cpp` first.
There is no AST visitor — protoST compiles keyword sends to bytecode
directly in `compileKeywordSend` (or its equivalent). The recognition
hook goes there.

### Step 1 — Recognise the form at the compile-send site

In the keyword-send compiler path, before falling through to
`emit SEND_KEYWORD`, check:
- `selector == "doYielding:"`
- argument count == 1
- the argument's AST node is a literal block (not a variable, not a
  computed expression)
- the block has exactly 1 formal parameter

If all match, route to **Step 2**. Otherwise, fall through to the
normal SEND_KEYWORD path (which will surface
`doesNotUnderstand: doYielding:` at runtime — intentional).

### Step 2 — Emit the loop bytecode

Allocate two fresh local slots (`i` and `n` — names mangled to avoid
collision with user locals). Compile the receiver expression once
into a local too (so the loop doesn't recompute it every iteration —
also stable across the iteration since collection values are
immutable). Emit the assembly above.

### Step 3 — Tests

`tests/conformance/do_yielding.st` (or similar in protoST's test
convention):

```smalltalk
"Basic correctness — same semantics as do: for trivial use."
| coll seen |
coll := #(10 20 30).
seen := OrderedCollection new.
coll doYielding: [ :x | seen add: x ].
seen printNl.   "Expected: (10 20 30)"

"Yield-through-iteration — the key new capability."
Object subclass: #Sink instanceVariableNames: 'count'.
Sink >> initialize count := 0. ^ self.
Sink >> ping count := count + 1. ^ count.

actors := OrderedCollection new.
1 to: 5 do: [ :i | | s | s := Sink new. s initialize.
                       actors add: (s asActor) ].

Object subclass: #Driver instanceVariableNames: 'actors'.
Driver >> setActors: a actors := a. ^ self.
Driver >> work
  | sum |
  sum := 0.
  actors doYielding: [ :a |
    sum := sum + ((a ping) wait) ].
  ^ sum.
"Expected: 1+1+1+1+1 = 5  (each fresh sink, count -> 1)"

d := Driver new. d setActors: actors.
((d asActor) work) wait.
```

Plus a regression test that confirms `aSet doYielding: [...]` raises
the expected `doesNotUnderstand:` (we want the fail-fast behaviour
to be tested, not accidental).

### Step 4 — Update multi_producer.st

`benchmarks/actors/multi_producer.st` currently has a FIXME and the
benchmark deliberately falls back to a hand-unrolled pattern. After
Step 3 passes, rewrite the driver to use `doYielding:` and remove the
FIXME. Measure throughput vs the pre-existing `mt100a` baseline; the
expectation per the projection in
`benchmarks/reports/2026-05-23-performance.md` is **~ 430 K msg/s
on this host** (6 cores × ~ 71.9 K per driver).

### Step 5 — Docs

- `docs/LANGUAGE.md` — describe `doYielding:` next to `do:`, with a
  short paragraph on when each is appropriate.
- `CHANGELOG.md` — `## 0.3.0 — yieldable iteration (date)` entry.
- `README.md` — replace "100 K+ class" with "1 M+ class with
  multi-producer" in the Performance section's hardware table; update
  the headline.

### Step 6 — Companion selectors (deferred)

Once `doYielding:` lands, the same desugaring works for:
- `doYielding:separatedBy:` (with a separator block)
- `doYieldingWithIndex:` (block takes element AND index)
- `selectYielding:` (collect into a new collection)
- `collectYielding:` (map)
- `injectYielding:into:` (fold)

These are separate commits. Land them iteratively.

## Edge cases and semantic preservation

### Empty collection
`coll size == 0` → loop test fails immediately → no block invocation
→ returns receiver. Matches `do:` semantics.

### Block that returns non-local (`^` inside `doYielding:`'s block)
The compiler emits the block with the enclosing method's
`__home_frame__`, exactly as it does for `do:` today. NonLocalReturn
unwinds out of the loop and the method, identical to current `do:`
behaviour.

### Block that itself contains `doYielding:`
Nested loops work — each level allocates its own `i` and `n` locals
in its own block-frame's local slots. Fully independent.

### Exception thrown inside the block
The exception propagates out of `SEND_KEYWORD value:`, out of the
loop bytecode, out to the enclosing method's `on:do:` if any. Same
exception semantics as `do:` today.

### `coll size` returns a non-integer
`SEND_BINARY <=` will surface `doesNotUnderstand:` if `i <= n` is not
satisfied by integer arithmetic. Acceptable — same outcome a user
calling `1 to: coll size do: [...]` would get.

### Block formal arg count mismatch
Compile-time check (the `args == 1` condition in Step 1) — if the
block has 0 or 2+ formals, fall through to normal SEND and the
runtime `doesNotUnderstand: doYielding:` surfaces. We choose
fail-fast.

### Collection mutated mid-iteration
Same as `do:` today — undefined for mutable receivers, well-defined
for immutable (the `at:` chain returns the snapshot value at the
time of the read). Since the loop reads `size` once at the top,
appends during iteration are NOT seen — same as a manual
`1 to: n do:` loop would do. Documented as intentional.

## Risks

1. **The recognition predicate misfires.** A user with `doYielding:`
   defined on their own prototype expecting custom behaviour gets
   our desugaring instead. Mitigation: document that `doYielding:`
   is reserved compiler-recognised.
2. **The block fast-path has a bug we haven't hit yet.** The
   inline block frame push at `ExecutionEngine.cpp:911` is the
   load-bearing assumption — if it ever crosses a primitive that
   recursively calls `invokeBlock`, we hit the same nested-engine
   problem. Mitigation: a test that puts `doYielding:` inside a
   `select:` (which is primitive) and confirms the inner
   `doYielding:` still yields correctly.
3. **`PUSH_BLOCK` emits a fresh BlockClosure per iteration.** Today
   `PUSH_BLOCK` may allocate; if so, the loop pays N allocs (one per
   iter). Acceptable in v1; an obvious follow-up is to hoist the
   block out of the loop body and reuse it.

## Out of scope for v1

- Loop unrolling.
- Block reuse / hoisting (the same block literal reused per
  iteration without re-PUSH_BLOCK).
- Constant folding when the receiver is a literal array of known
  size (`#(a b c) doYielding: [...]`).
- A type-inference path that desugars `do:` automatically when the
  compiler can prove the receiver is SequenceableCollection.
- Companion selectors (Step 6) — those are separate landings.

## Success criteria

After this lands:

1. `tests/conformance/do_yielding.st` passes (correctness +
   yieldability).
2. `multi_producer.st` runs without the FIXME, with measurable
   throughput improvement over `mt100a`.
3. No regression in the existing 751 ctest suite.
4. `mt100a` itself sees no measurable change (we are NOT touching
   `do:`).

## Estimated effort

- Step 1 (parser/compiler recognition): 1-2 hours
- Step 2 (bytecode emit): 1-2 hours
- Step 3 (tests): 1 hour
- Step 4 (benchmark update + measurement): 30 min
- Step 5 (docs): 1 hour
- **Total: 4-7 hours of focused work**, ideal for one fresh session.

The risk profile is "feature add, not refactor" — the only failure
mode is "the desugaring doesn't match `do:` semantics in some edge
case we missed", which a focused test pass catches.
