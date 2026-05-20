# Collection Hierarchy — Design Spec

**Track 2** (see `docs/ROADMAP.md`). Depends on closures (now working) and
benefits from exceptions (Track 1, done).

## Goal

A Smalltalk collection hierarchy and iteration protocol, built on protoCore's
collection primitives — `Array`, `OrderedCollection`, `Interval`, `Set`,
`Bag`, `Dictionary`, with `do:` / `collect:` / `select:` / `reject:` /
`detect:` / `inject:into:` and friends. Plus the literal syntax `#(…)` and
`{…}`, which the parser already accepts but the compiler does not yet lower.

## What exists today

- **No language-level collections.** Bootstrap has only `Object … Exception`.
- The parser DOES build `ArrayLit` (`#(…)`) and `DynArrayLit` (`{…}`) AST
  nodes — but the compiler never lowers them (a gap).
- Zero iteration protocol bound. protoST uses protoCore lists only internally
  (actor mailboxes, block args).
- protoCore provides `ProtoList`, `ProtoTuple`, `ProtoSparseList`, `ProtoSet`,
  `ProtoMultiset`, `ProtoString` — **all immutable, structural-sharing**, with
  iterators and `ProtoContext` factories.

## The class hierarchy

Bootstrap-installed prototypes (like `Exception` was), each `__class_name__`-d
and user-subclassable:

```
Object
  Collection                 (abstract — the shared iteration protocol)
    SequenceableCollection    (abstract — ordered, indexable)
      Array                   ← ProtoList   (fixed size; at:put: mutates a slot)
      OrderedCollection       ← ProtoList   (growable: add:, removeFirst, …)
      Interval                ← lazy        (1 to: 10 [by: 2]; no backing store)
    HashedCollection          (abstract)
      Set                     ← ProtoSet
      Bag                     ← ProtoMultiset
      Dictionary              ← ProtoSparseList (object keys — see COL-d)
```

## The mutability pattern (from protoPython)

protoCore collections are immutable. A protoST collection is a **mutable
`ProtoObject`** (a child of its class prototype) holding the immutable
primitive under a `__data__` attribute. A mutating operation (`add:`,
`at:put:`, `removeFirst`, …) replaces `__data__` with the new immutable
snapshot returned by the protoCore mutator — copy-on-write, O(log n) via the
structural sharing, no per-element rewrite. `Interval` has no `__data__` (it
stores `start`/`stop`/`step` and computes elements lazily).

## Base operations vs. derived protocol

- **Base operations are C++ primitives**, one set per concrete collection,
  thin over protoCore: `do:`, `size`, `isEmpty`, `at:` / `at:put:` (sequenceable),
  `add:` / `remove:` / `includes:` (per collection), `asArray`, `species`.
- **The derived iteration protocol is written once**, bound on `Collection`,
  inherited by all: `collect:`, `select:`, `reject:`, `detect:`,
  `detect:ifNone:`, `inject:into:`, `do:separatedBy:`, `count:`, `anySatisfy:`,
  `allSatisfy:`, `isEmpty`/`notEmpty`, `asArray`, `,` (concatenation).
  Implementation: a C++ helper `forEachElement(collection, callback)` that
  dispatches on the receiver's collection kind and iterates the right
  protoCore iterator; every derived primitive is built on it plus the user
  block. `detect:` with no match signals an `Error` (Track 1) — `detect:ifNone:`
  takes a fallback block.
  *(A future refinement could rewrite the derived protocol as a Smalltalk
  prelude — `Collection>>collect:` in Smalltalk over `do:`/`add:`. Out of scope
  here; C++ primitives keep the MVP simple and DRY.)*

## Literals

The compiler must lower the existing AST nodes:
- `#(1 2 3 $a 'str' #sym)` (`ArrayLit`) → an `Array` of the literal elements.
  Elements are literals only (numbers, chars, strings, symbols; bare
  identifiers inside `#(…)` are symbols).
- `{ expr1. expr2. expr3 }` (`DynArrayLit`) → an `Array` built by evaluating
  each expression at runtime.
Both produce an `Array`. A new opcode (or a primitive `Array class>>withAll:`
fed from the operand stack) builds the instance — pick the simplest lowering.

## Sub-slices

Land in order; each is a coherent, testable unit.

- **COL-a — Foundation + Array + iteration protocol + literals.**
  The abstract prototypes (`Collection`, `SequenceableCollection`,
  `HashedCollection`); `Array` concrete (`at:`, `at:put:`, `size`, `do:`,
  `Array class>>new:`, `withAll:`); the shared derived protocol on
  `Collection`; `#(…)` and `{…}` lowering. This is the big foundational slice.
- **COL-b — OrderedCollection.** Growable: `add:`, `addFirst:`, `addLast:`,
  `addAll:`, `removeFirst`, `removeLast`, `remove:`, `at:`/`at:put:`,
  `OrderedCollection class>>new`.
- **COL-c — Set + Bag.** `Set` ← `ProtoSet`; `Bag` ← `ProtoMultiset`.
  `add:`, `remove:`, `includes:`, `size`, `occurrencesOf:` (Bag), `do:`.
- **COL-d — Dictionary.** ← `ProtoSparseList`. The hard one: `ProtoSparseList`
  keys are `unsigned long`, but Smalltalk dictionary keys are arbitrary
  objects. Resolve object-key → slot (hash + collision buckets, or whatever
  protoPython's `dict` does — study it first). `at:`, `at:put:`, `at:ifAbsent:`,
  `removeKey:`, `includesKey:`, `keysDo:`, `valuesDo:`, `keysAndValuesDo:`,
  `associationsDo:`, `keys`, `values`.
- **COL-e — Interval + `Number` iteration.** `1 to: 10`, `1 to: 10 by: 2`
  (an `Interval`, lazy), and `Number>>to:do:` / `to:by:do:`.

`String` is already indexable; folding it into the `Collection` protocol
(so `'abc' do: […]`, `collect:` over chars) is a small optional follow-on —
note it but it need not block the slices.

## Tests

Per sub-slice — at minimum:
- COL-a: `#(1 2 3) size` → 3; `at:`/`at:put:`; `{1+1. 2*2. 3} ` evaluates;
  `do:` sums elements; `collect:` maps; `select:`/`reject:` filter;
  `detect:`/`detect:ifNone:`; `inject:into:` sums; `do:separatedBy:`;
  `,` concatenates; `isEmpty`; `detect:` with no match signals an `Error`
  catchable by `on:do:`.
- COL-b: `add:` grows; `removeFirst`/`removeLast`; `addAll:`; round-trip
  `asArray`.
- COL-c: `Set` dedups; `includes:`; `Bag` counts duplicates (`occurrencesOf:`).
- COL-d: `at:put:` then `at:`; `at:ifAbsent:`; object keys (symbols, strings,
  integers) all work; `keysAndValuesDo:`.
- COL-e: `1 to: 5` iterated with `do:` yields 1..5; `1 to: 10 by: 2`;
  `Number>>to:do:`.
- Closures: every iteration test uses a block that closes over a method
  local / argument (the capture path fixed just before this track) — e.g.
  `inject:into:` accumulating into a method temp.
- Regression: actors, the demo, exceptions, the existing suite stay green.
