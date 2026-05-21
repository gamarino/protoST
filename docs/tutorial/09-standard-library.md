# Chapter 9 — The standard library

[Tutorial index](../TUTORIAL.md) · Previous: [Chapter 8](08-collections.md) · Next: [Chapter 10 — Actors and futures](10-actors-and-futures.md)

---

A language is only as productive as the toolkit that ships with it. This
chapter covers protoST's standard library: the mathematical protocol built into
every number, and the four loadable modules — `Stream`, `Random`, `JSON`, and
`Time` — that you pull in with `Import from:`. It also explains the module
system that delivers them, which you will use again in
[Chapter 12](12-tooling.md).

## 9.1 Two kinds of library

protoST's standard functionality comes in two forms, and the distinction
matters:

1. **Always-available protocol.** Some functionality is bound directly onto the
   built-in classes and needs no import. The mathematical protocol on `Number`
   is the main example — `2 sqrt` just works, anywhere, with no setup.
2. **Loadable modules.** Larger, optional functionality lives in `.st` files
   under `lib/` and is pulled in explicitly with `Import from:`. `Stream`,
   `Random`, `JSON`, and `Time` are modules.

> **In Python** the analogue of (1) is the *builtins* (`len`, `abs`, `+`) and
> of (2) is `import math`, `import json`, `import random`. **In JavaScript**,
> (1) is `Math`/operators and (2) is `import`/`require`. protoST's split is the
> same idea — a small always-on core, a larger explicitly-imported rest — but
> note that even the "always-on" maths is *messages on numbers*, not a `Math`
> namespace object.

## 9.2 The mathematical protocol

Every number — `SmallInteger`, `LargeInteger`, `Float` — answers the full
mathematical protocol, because it is bound once on their shared superclass
`Number`. No import; the messages are always there.

```bash
$ ./build/protost -e '2 sqrt'
1.4142135623730951
$ ./build/protost -e 'Float pi'
3.141592653589793
$ ./build/protost -e '(48 gcd: 18)'
6
```

The protocol, grouped by purpose:

| Group | Selectors |
|-------|-----------|
| Roots / powers | `sqrt`, `squared`, `raisedTo:` |
| Transcendental | `sin` `cos` `tan`, `arcSin` `arcCos` `arcTan`, `ln`, `exp`, `log`, `log:` |
| Rounding | `floor`, `ceiling`, `rounded`, `truncated` |
| Sign / parity | `abs`, `negated`, `sign`, `isZero`, `even`/`odd`, `isEven`/`isOdd` |
| Comparison helpers | `min:`, `max:`, `between:and:` |
| Conversion | `asFloat`, `asInteger`, `asCharacter` |
| Integer-specific | `factorial`, `gcd:`, `lcm:`, `reciprocal` |

Two properties are worth dwelling on, because they make protoST arithmetic
*correct* in cases where other languages quietly are not.

**Exact exponentiation and factorial.** `raisedTo:` with a non-negative integer
exponent, and `factorial`, are computed by exact repeated multiplication. Each
intermediate product promotes to a `LargeInteger` the moment it leaves the
56-bit `SmallInteger` range — so the answer is *always* exact, never an
overflowed `double`:

```bash
$ ./build/protost -e '(2 raisedTo: 64)'
18446744073709551616
```

`2^64` is computed to the digit. (`raisedTo:` with a *float* exponent, or a
negative one, routes through libm `pow` and answers a `Float`.)

**Domain errors are IEEE-754, not exceptions.** A libm domain error — `(-1)
sqrt`, `0 ln` — does *not* raise a protoST `Error`. It yields the IEEE-754
result, `nan` or `inf`, the same total contract libm offers. The maths protocol
never raises a domain error. Genuinely invalid *arguments* — `factorial` of a
negative integer, a non-numeric argument — still raise a catchable `Error`.

> **In Python** integers are arbitrary-precision so `2**64` is exact, but
> `math.sqrt(-1)` *raises* `ValueError`. **In JavaScript** `2**64` loses
> precision (it is a float) and `Math.sqrt(-1)` returns `NaN`. **In protoST**
> you get the best of both: `2 raisedTo: 64` is exact like Python, and
> `(-1) sqrt` is `nan` like JavaScript — exact integer arithmetic, total
> floating-point maths.

`Float` also carries class-side constants: `Float pi`, `Float e`,
`Float infinity`, `Float nan`.

## 9.3 The module system: `Import from:`

A `.st` file is a *module*. Loading it runs its top-level forms and gives you a
module object whose attributes are the (non-`_`-prefixed) names it defined —
primarily its classes. You load one with `Import from:`:

```smalltalk
m := Import from: 'stream'.
```

`Import` is a global object; `Import from: 'stream'` resolves the module named
`stream`, loads it once (imports are cached — importing the same name twice
yields the same module object), and answers the module. You then read its
exported classes with ordinary unary sends: `m ReadStream` reads the
`ReadStream` attribute off the module.

The runtime finds the standard `lib/` directory automatically — no environment
variable is needed for a normal build — so `Import from: 'stream'` finds
`lib/stream.st` without any path. A module of the same name in your working
directory or active venv shadows the stdlib one.

> **In Python** `import json` binds a module object to the name `json`, and you
> reach into it with `json.loads`. **In JavaScript**, `import * as json from
> 'json'`. **In protoST** `m := Import from: 'json'` binds the module to a
> variable *you* name, and you reach in with a *message send*: `m JSON`. The
> module is a plain object; reading a class out of it is the same dotless
> unary send you use everywhere else.

The four standard modules:

| Module | Import | Provides |
|--------|--------|----------|
| `stream` | `Import from: 'stream'` | `ReadStream`, `WriteStream` |
| `random` | `Import from: 'random'` | `Random` — a seedable PRNG |
| `json` | `Import from: 'json'` | `JSON` — `parse:` and `stringify:` |
| `time` | `Import from: 'time'` | `Time`, `Timestamp`, `Duration` |

## 9.4 `Stream` — cursor-based collection access

The `stream` module provides `ReadStream` (sequential reading over a fixed
collection) and `WriteStream` (sequential appending into a growing one). A
stream is a *cursor*: it remembers a position so you can consume a collection
piece by piece.

```smalltalk
"-- stream-demo.st --"
m := Import from: 'stream'.

rs := m ReadStream on: #(10 20 30 40).
first := rs next.
second := rs next.

ws := m WriteStream new.
ws nextPut: 100; nextPut: 200.

{ first. second. rs atEnd. ws contents size }.
```

```bash
$ ./build/protost stream-demo.st
an Array
```

`ReadStream on:` wraps a collection; `next` answers the element at the cursor
and advances. `WriteStream new` gives an empty stream; `nextPut:` appends.
`atEnd` tests for exhaustion, `contents` answers the accumulated data. The four
facts in the array are `10`, `20`, `false`, and `2`.

> **In Python** a `ReadStream` is roughly an *iterator* (`next()`); a
> `WriteStream` is an `io.StringIO`/list you append to. **In JavaScript**, a
> generator and an array. protoST's streams package the "remember where I am"
> idiom as a small, explicit object.

## 9.5 `Random` — deterministic pseudo-random numbers

The `random` module provides `Random`, a **deterministic, seedable**
pseudo-random generator: a given seed always produces the same sequence, on
every run and platform. (It is not cryptographic — do not use it for security.)

```smalltalk
"-- random-demo.st --"
m := Import from: 'random'.
r := m Random seed: 42.
{ r nextInt: 6. r nextInt: 6. r between: 100 and: 200 }.
```

```bash
$ ./build/protost random-demo.st
an Array
```

`Random seed:` builds a generator from an integer seed; `Random new` uses a
fixed default seed (so even `new` is reproducible). The protocol: `next` (a
`Float` in [0, 1)), `nextInt: n` (an integer in 1..n), `between: lo and: hi`
(an integer in that inclusive range), `next: n` (an `Array` of `n` floats).

> **In Python** `random.seed(42)` then `random.randint(1, 6)`. **In
> JavaScript**, `Math.random()` — which is *not* seedable, so reproducible
> randomness needs a library. protoST's `Random` is seedable by design, which
> makes it a good fit for deterministic simulations and tests — exactly the
> kind of thing a digital twin needs.

## 9.6 `JSON` — parse and stringify

The `json` module provides `JSON`, with two class-side messages:

- `JSON parse:` turns a JSON document string into protoST objects — a
  `Dictionary` for an object, an `Array` for an array, plus strings, numbers,
  booleans, and `nil`, recursively to any depth.
- `JSON stringify:` is the inverse — a protoST value back to a JSON string.

```smalltalk
"-- json-demo.st --"
m := Import from: 'json'.

doc := m JSON parse: '{"name": "Ada", "scores": [90, 85, 88]}'.
name := doc at: 'name'.
scores := doc at: 'scores'.

back := m JSON stringify: #(1 2 3).

{ name. scores size. back }.
```

```bash
$ ./build/protost json-demo.st
an Array
```

`parse:` of the object string gives a `Dictionary`; `doc at: 'name'` is the
string `'Ada'`, `doc at: 'scores'` is a three-element `Array`. `stringify:` of
`#(1 2 3)` gives the string `'[1,2,3]'`. The three array facts are `'Ada'`,
`3`, and `'[1,2,3]'`.

> **In Python** this is `json.loads` / `json.dumps`. **In JavaScript**,
> `JSON.parse` / `JSON.stringify` — and protoST deliberately mirrors the
> JavaScript spelling, `JSON parse:` / `JSON stringify:`, because it is the
> name every web developer already knows. A JSON object becomes a
> `Dictionary`, a JSON array becomes an `Array` — the natural protoST
> counterparts.

## 9.7 `Time` — clocks, timestamps, durations

The `time` module provides wall-clock access and a small instant/span object
model:

- `Time now` answers a `Timestamp` for the current instant.
- `Time millisecondsToRun: aBlock` evaluates the block and answers how many
  milliseconds it took (measured on a monotonic clock).
- `Timestamp` wraps an instant; `Duration` wraps a span. They compose:
  `Timestamp - Timestamp` → `Duration`, `Timestamp + Duration` → `Timestamp`.

```smalltalk
"-- time-demo.st --"
m := Import from: 'time'.

d := m Duration seconds: 90.
longer := d + (m Duration minutes: 2).

elapsed := m Time millisecondsToRun: [
  1 to: 100000 do: [ :i | i * i ] ].

{ d asSeconds. longer asSeconds. elapsed >= 0 }.
```

```bash
$ ./build/protost time-demo.st
an Array
```

`Duration seconds: 90` is a 90-second span; adding a 2-minute `Duration` gives
a 210-second one. `millisecondsToRun:` benchmarks the block. The three facts
are `90`, `210`, and `true`.

The `Time` module is deliberately minimal — there are **no timezones and no
calendar**. A `Timestamp` is an opaque instant (epoch milliseconds); a
`Duration` is an opaque span. They compare, subtract, and add — that is the
whole model. `millisecondsToRun:` is the building block for the `:time` REPL
command you will meet in [Chapter 12](12-tooling.md).

## 9.8 Reading the modules as protoST code

One thing worth knowing: the four standard modules are *not* C++ — they are
ordinary protoST `.st` files in `lib/`, written in exactly the language this
tutorial teaches. `lib/random.st` is a 30-line linear-congruential generator;
`lib/stream.st` is two small classes. You can open them, read them, and learn
from them — they are the canonical examples of idiomatic protoST module code,
and they show the class-side-constructor pattern from
[Chapter 5](05-classes-and-methods.md) in real use (`ReadStream class >> on:`,
`Random class >> seed:`, `WriteStream class >> new` overriding `new` to call
`initialize` itself).

Writing your own module is the same: create a `.st` file, declare classes, and
every non-`_`-prefixed class becomes an attribute of the module object that
`Import from:` returns.

> **No `Transcript`.** Standard Smalltalk's `Transcript` output stream is *not*
> provided in protoST. To print, use `printNl` — it prints the receiver
> followed by a newline and answers the receiver. This is a documented
> deviation; [Chapter 14](14-for-the-smalltalk-programmer.md) lists it.

## 9.9 Summary

- protoST's library is in two parts: the **always-available** mathematical
  protocol bound on `Number`, and **loadable modules** pulled in with
  `Import from:`.
- The maths protocol is exact where it can be — `raisedTo:` and `factorial`
  promote to `LargeInteger` and never overflow — and total where it cannot —
  domain errors yield `nan`/`inf`, not exceptions.
- `Import from: 'name'` loads a module (cached) and answers a module object;
  read its classes with unary sends (`m JSON`).
- The four standard modules: `stream` (`ReadStream`/`WriteStream`), `random`
  (a seedable PRNG), `json` (`parse:`/`stringify:`), `time`
  (`Time`/`Timestamp`/`Duration`).
- The modules are plain protoST `.st` files in `lib/` — readable, and the model
  for writing your own.

---

Next: [Chapter 10 — Actors and futures](10-actors-and-futures.md)
