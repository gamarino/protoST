# Chapter 8 — Collections

[Tutorial index](../TUTORIAL.md) · Previous: [Chapter 7](07-exceptions.md) · Next: [Chapter 9 — The standard library](09-standard-library.md)

---

Almost every real program is, underneath, the manipulation of collections of
things. protoST gives you a family of collection classes and — more
importantly — a single, uniform **iteration protocol** that every one of them
understands. Learn the protocol once and you can iterate, transform, filter,
and fold any collection in the language.

## 8.1 The collection family

protoST's collections form a small hierarchy under the abstract class
`Collection`:

```
Collection                  "abstract — defines the iteration protocol"
  SequenceableCollection     "abstract — ordered, integer-indexed"
    Array                    "fixed-size, indexed"
    OrderedCollection        "growable, indexed"
    Interval                 "a lazy arithmetic sequence"
  HashedCollection           "abstract"
    Set                      "deduplicating"
    Bag                      "counts duplicates"
    Dictionary               "key → value map"
```

`Association` (a single key/value pair) sits beside them, a direct child of
`Object`.

> **In Python** the rough map is: `Array` ≈ `tuple` (fixed) / `Array` and
> `OrderedCollection` together ≈ `list`, `Set` ≈ `set`, `Dictionary` ≈ `dict`,
> `Bag` ≈ `collections.Counter`, `Interval` ≈ `range`. **In JavaScript**:
> `OrderedCollection` ≈ `Array`, `Set` ≈ `Set`, `Dictionary` ≈ `Map`. The
> shapes you know carry over — what changes is that *every* collection here
> answers the *same* iteration messages, with no `for`/comprehension/`.map()`
> split.

## 8.2 Indexing is 1-based

This is the first thing to internalise, because it differs from Python and
JavaScript and it is silent — an off-by-one will not error, it will just be
wrong:

> **Sequenceable collections are 1-indexed.** `at: 1` is the *first* element.

```bash
$ ./build/protost -e '#(10 20 30) at: 2'
20
```

`at: 2` is the *second* element. An index of `0`, or one past the end, signals
a catchable `Error`.

> **In Python/JS** indexing is 0-based: `arr[0]` is the first element. **In
> protoST** — as in classic Smalltalk, Lua, and mathematics — it is 1-based:
> `arr at: 1` is the first. There is no negative-index "from the end" form;
> use `last` for the last element.

## 8.3 `Array` — fixed-size, indexed

You met array *literals* in [Chapter 3](03-variables-and-literals.md): `#(1 2
3)` (all-literal) and `{ 1 + 1. x }` (computed). An `Array` can also be built
from class-side constructors:

| Constructor | Result |
|-------------|--------|
| `Array new: n` | an `Array` of `n` `nil`s |
| `Array with: a with: b …` | an `Array` of the given elements (up to four `with:`) |
| `Array withAll: aCollection` | an `Array` copying another collection |

```bash
$ ./build/protost -e '(Array with: 1 with: 2 with: 3) size'
3
```

An `Array`'s size is *fixed*. `at:put:` replaces an existing slot but cannot
grow the array:

```smalltalk
"-- array-slot.st --"
a := Array new: 3.
a at: 1 put: 99.
a at: 1.
```

```bash
$ ./build/protost array-slot.st
99
```

When you need to *grow* a collection, reach for `OrderedCollection`.

## 8.4 `OrderedCollection` — growable, indexed

`OrderedCollection` is the workhorse: an ordered, integer-indexed collection
that grows and shrinks. It is what you would reach for a Python `list` or a
JavaScript `Array` to do.

```smalltalk
"-- ordered.st --"
oc := OrderedCollection new.
oc add: 1; add: 2; add: 3.
oc removeFirst.
{ oc size. oc first. oc last }.
```

```bash
$ ./build/protost ordered.st
an Array
```

The script builds an `OrderedCollection`, appends three elements with a
*cascade* (`add: 1; add: 2; add: 3` — three messages to the same receiver, see
[Chapter 2](02-objects-and-messages.md)), removes the first, then evaluates a
dynamic array of three facts about it. The printed `an Array` is that dynamic
array's *class* — printing a collection shows its class name, not its contents
(§8.9 shows how to see the contents).

The key `OrderedCollection` protocol:

| Message | Effect |
|---------|--------|
| `add:` / `addLast:` | append an element |
| `addFirst:` | prepend an element |
| `addAll:` | append every element of another collection |
| `removeFirst` / `removeLast` | remove and return an end element |
| `remove:` | remove a matching element (`Error` if absent) |
| `remove:ifAbsent:` | remove, with a fallback block if absent |
| `at:` / `at:put:` / `first` / `last` / `size` | as you would expect |

> **In Python** `oc add: x` is `list.append(x)`, `addFirst:` is `insert(0, x)`,
> `removeFirst` is `pop(0)`. **In JavaScript**, `push` / `unshift` / `shift`.
> The names differ; the data structure is the same growable sequence.

## 8.5 `Set` and `Bag`

A `Set` is a deduplicating collection — adding an element already present is a
no-op:

```smalltalk
"-- set.st --"
s := Set new.
s add: 1; add: 1; add: 2; add: 2; add: 3.
s size.
```

```bash
$ ./build/protost set.st
3
```

Five `add:`s, but `size` is `3` — duplicates collapsed. `Set` also answers
`includes:` for a membership test and `remove:` to drop an element.

A `Bag` is the opposite choice: it *counts* duplicates instead of collapsing
them.

```smalltalk
"-- bag.st --"
b := Bag new.
b add: 5.
b add: 5.
b add: 7.
{ b size. b occurrencesOf: 5 }.
```

```bash
$ ./build/protost bag.st
an Array
```

Here `b size` is `3` (a `Bag` counts every occurrence) and `b occurrencesOf: 5`
is `2`. `Bag` also has `add:withOccurrences:` to add several at once.

> **In Python** a `Set` is `set` and a `Bag` is `collections.Counter`. **In
> JavaScript** a `Set` is `Set` and a `Bag` has no built-in — you would use a
> `Map` of counts. The protoST `Bag` is exactly that, packaged as a class.

## 8.6 `Dictionary` — key → value

A `Dictionary` maps arbitrary keys to values. Keys may be symbols, strings,
integers, or any object.

```smalltalk
"-- dict.st --"
d := Dictionary new.
d at: #one put: 1.
d at: #two put: 2.
{ d at: #one. d includesKey: #two. d at: #missing ifAbsent: [ 0 ] }.
```

```bash
$ ./build/protost dict.st
an Array
```

The three facts in the dynamic array are `1`, `true`, and `0`. The key
`Dictionary` protocol:

| Message | Effect |
|---------|--------|
| `at:put:` | store a key/value |
| `at:` | the value for a key — an *absent* key signals an `Error` |
| `at:ifAbsent:` | the value, or the fallback block's value if absent |
| `at:ifAbsentPut:` | the value, computing *and storing* the fallback if absent |
| `removeKey:` / `removeKey:ifAbsent:` | remove a key |
| `includesKey:` | key-presence test |
| `keys` / `values` / `associations` | collections of the parts |
| `keysAndValuesDo:` | iterate over key/value pairs |

> **The absent-key trap.** In Python `d[k]` raises `KeyError` for a missing
> key, and `d.get(k, default)` is the safe form. **In protoST** `d at: k`
> likewise signals an `Error` for a missing key, and `d at: k ifAbsent:
> [ default ]` is the safe form — the fallback is a *block*, evaluated only
> when the key is absent. `at:ifAbsentPut:` is the "compute-once cache" idiom:
> it stores the fallback so the next lookup finds it.

An `Association` — a single key/value pair — is built with the `->` operator
and answers `key` and `value`:

```bash
$ ./build/protost -e '(#answer -> 42) value'
42
```

## 8.7 `Interval` — a lazy sequence

An `Interval` is an arithmetic sequence that stores only its `start`, `stop`,
and `step` — it computes elements on demand and keeps no backing array. You
create one with `to:` or `to:by:` on a number:

```bash
$ ./build/protost -e '(1 to: 5) size'
5
$ ./build/protost -e '(1 to: 10 by: 2) last'
9
```

`1 to: 5` is the sequence 1, 2, 3, 4, 5; `1 to: 10 by: 2` is 1, 3, 5, 7, 9. An
`Interval` is a full collection — it answers `do:`, `collect:`, `at:`, and the
rest — so you can iterate it without ever materialising the numbers into an
array.

> **In Python** this is exactly `range(1, 6)` — lazy, stores only its bounds.
> **In JavaScript** there is no built-in equivalent. protoST's `Interval` is
> Python's `range`, and `1 to: 5 do: [ :i | … ]` from [Chapter 4](04-blocks.md)
> is "iterate that interval" written as a single fused message.

## 8.8 The iteration protocol — the heart of the chapter

Every collection — `Array`, `OrderedCollection`, `Set`, `Bag`, `Dictionary`,
`Interval` — inherits one shared protocol from `Collection`. These are the
messages you will use every day. Each takes a *block*; the collection runs the
block against its elements.

### `do:` — visit each element

```smalltalk
"-- sum-do.st --"
sum := 0.
#(10 20 30) do: [ :e | sum := sum + e ].
sum.
```

```bash
$ ./build/protost sum-do.st
60
```

`do:` evaluates the block once per element and answers the receiver. It is the
foundation; everything below could be built from it.

### `collect:` — transform (map)

`collect:` answers a *new* collection of the block's results — one result per
element:

```bash
$ ./build/protost -e '(#(1 2 3 4) collect: [ :e | e * e ]) size'
4
```

`#(1 2 3 4) collect: [ :e | e * e ]` produces an array of the squares,
`1 4 9 16`.

> **In Python** `collect:` is `[e*e for e in xs]` or `map(...)`. **In
> JavaScript** it is `xs.map(e => e*e)`. **In protoST** it is `xs collect:
> [ :e | e * e ]` — the same map operation, written as a message taking a
> block.

### `select:` and `reject:` — filter

`select:` keeps the elements for which the block answers `true`; `reject:` keeps
those for which it answers `false`:

```bash
$ ./build/protost -e '(#(1 2 3 4 5 6) select: [ :e | e isEven ]) size'
3
```

The even elements `2 4 6` are kept — `size` is `3`. `reject:` of the same block
would keep `1 3 5`.

> **In Python** `select:` is `[e for e in xs if cond]` or `filter(...)`. **In
> JavaScript** it is `xs.filter(...)`. `reject:` is the negation — Python and
> JS have no dedicated "filter-out" so you negate the predicate; protoST gives
> you both directions a name.

### `detect:` — find the first match

`detect:` answers the *first* element satisfying the block. If none matches it
signals an `Error` — unless you use `detect:ifNone:`, which evaluates a fallback
block instead:

```bash
$ ./build/protost -e '(#(3 1 4 1 5 9) detect: [ :e | e > 3 ])'
4
$ ./build/protost -e '(#(1 2 3) detect: [ :e | e > 9 ] ifNone: [ -1 ])'
-1
```

### `inject:into:` — fold (reduce)

`inject:into:` folds a collection down to a single value. The first argument is
the seed; the block receives the running accumulator and each element, and
answers the new accumulator:

```bash
$ ./build/protost -e '(#(1 2 3 4 5) inject: 0 into: [ :acc :each | acc + each ])'
15
```

Seed `0`; the block adds each element into the accumulator; the final
accumulator is `15`. Fold the same array with seed `1` and `[ :acc :each | acc
* each ]` and you get `120` — the product. `inject:into:` is the most general
of the iteration messages: `collect:`, `select:`, `count:` and the rest are all
expressible as folds.

> **In Python** this is `functools.reduce(lambda acc, e: acc + e, xs, 0)`. **In
> JavaScript**, `xs.reduce((acc, e) => acc + e, 0)`. **In protoST** it is
> `xs inject: 0 into: [ :acc :e | acc + e ]` — same fold, the seed and the
> two-argument block laid out as a keyword message.

### Predicates and counts

| Message | Answers |
|---------|---------|
| `count: aBlock` | how many elements satisfy the block |
| `anySatisfy: aBlock` | `true` if *any* element satisfies it |
| `allSatisfy: aBlock` | `true` if *every* element satisfies it |
| `isEmpty` / `notEmpty` | emptiness tests |
| `size` | the element count |

```bash
$ ./build/protost -e '(#(1 2 3 4 5 6) count: [ :e | e > 3 ])'
3
$ ./build/protost -e '(#(2 4 6 8) allSatisfy: [ :e | e isEven ])'
true
```

### `do:separatedBy:` and `,`

`do:separatedBy:` runs a second block *between* elements — useful for building
delimited output. `,` concatenates two collections into a new one:

```bash
$ ./build/protost -e '(#(1 2 3) , #(4 5)) size'
5
```

### Species — what `collect:` gives back

`collect:`, `select:`, and `reject:` answer a collection of the receiver's own
*species*: an `Array` from an `Array`, an `OrderedCollection` from an
`OrderedCollection`, a `Set` from a `Set`. The transformation does not change
the *kind* of collection — only its contents.

## 8.9 Inspecting a collection's contents

You will have noticed that printing a collection — directly, or via
`printString` — shows its *class* (`an Array`, `an OrderedCollection`), not its
elements. To see the contents, build a string yourself, or iterate. A compact
idiom is to fold the elements into a string:

```smalltalk
"-- show.st --"
nums := #(3 1 4 1 5).
text := nums inject: '' into: [ :acc :e | acc , e printString , ' ' ].
text.
```

```bash
$ ./build/protost show.st
3 1 4 1 5 
```

`inject:into:` with an empty-string seed and a block that appends each element's
`printString` gives you a readable rendering. The same pattern, with a class
overriding `printString`, is how you make your own objects print informatively
([Chapter 5](05-classes-and-methods.md)).

## 8.10 A worked example — word frequencies

Putting the protocol together. Count how many numbers in a list exceed a
threshold, two ways:

```smalltalk
"-- threshold.st --"
readings := #(12 47 8 91 33 60 5 77).

"Way 1 — select then size."
highBySelect := (readings select: [ :r | r > 40 ]) size.

"Way 2 — inject:into: as a counting fold."
highByFold := readings inject: 0 into: [ :acc :r |
  (r > 40) ifTrue: [ acc + 1 ] ifFalse: [ acc ] ].

{ highBySelect. highByFold }.
```

```bash
$ ./build/protost threshold.st
an Array
```

Both paths compute `4` (the readings `47 91 60 77` exceed `40`). The first
filters then counts; the second folds, incrementing the accumulator
conditionally. Two ways to the same answer — and a good illustration that
`select:` and friends are *conveniences over* the more fundamental
`inject:into:`.

## 8.11 Summary

- The collection family: `Array` (fixed), `OrderedCollection` (growable),
  `Interval` (lazy range), `Set` (dedup), `Bag` (counts), `Dictionary` (map),
  plus `Association` (one pair).
- Sequenceable collections are **1-indexed** — `at: 1` is the first element.
- One shared **iteration protocol** works on every collection: `do:` (visit),
  `collect:` (map), `select:` / `reject:` (filter), `detect:` / `detect:ifNone:`
  (find), `inject:into:` (fold), `count:` / `anySatisfy:` / `allSatisfy:`
  (predicates). Each takes a block.
- `collect:` / `select:` / `reject:` preserve the receiver's species.
- A `Dictionary` lookup of an absent key signals an `Error`; `at:ifAbsent:`
  (and `at:ifAbsentPut:`) is the safe form.
- Printing a collection shows its class; fold with `inject:into:` to render its
  contents.

---

Next: [Chapter 9 — The standard library](09-standard-library.md)
