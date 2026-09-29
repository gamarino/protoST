# Chapter 9 — The standard library

[Tutorial index](../TUTORIAL.md) · Previous: [Chapter 8](08-collections.md) · Next: [Chapter 10 — Actors and futures](10-actors-and-futures.md)

---

A language is only as productive as the toolkit that ships with it. This
chapter covers protoST's standard library: the kernel classes that are always
there — the mathematical protocol on every number, streams, `Random`, time and
dates — and the loadable modules, `JSON` among them, that you pull in with
`Import from:`. It also explains the module system that delivers them, which
you will use again in [Chapter 12](12-tooling.md).

## 9.1 Two kinds of library

protoST's standard functionality comes in two forms, and the distinction
matters:

1. **The kernel.** The built-in classes and their protocol need no import:
   numbers and their mathematical protocol (`2 sqrt` just works), collections,
   strings and `Character`, `ReadStream` / `WriteStream`, `Random`, `Time`,
   `Date`, `Duration`, `Transcript`. Much of the kernel is itself written in
   protoST, in `lib/kernel/`, and loaded when the runtime starts.
2. **Loadable modules.** Optional functionality lives in `.st` files under
   `lib/` and is pulled in explicitly with `Import from:`. `json` is the one
   you will use; `stream`, `random` and `time` are older modules kept for
   compatibility (§9.4, §9.5, §9.7).

> **In Python** the analogue of (1) is the *builtins* (`len`, `abs`, `+`) and
> of (2) is `import math`, `import json`, `import random`. **In JavaScript**,
> (1) is `Math`/operators and (2) is `import`/`require`. protoST's split is the
> same idea — a small always-on core, a larger explicitly-imported rest — but
> note that even the "always-on" maths is *messages on numbers*, not a `Math`
> namespace object.

## 9.2 The mathematical protocol

Every number — `SmallInteger`, `LargeInteger`, `Fraction`, `Float` — answers
the full mathematical protocol, because it is bound once on their shared
superclass `Number`. No import; the messages are always there.

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
| Integer-specific | `factorial`, `gcd:`, `lcm:`, `bitAnd:` `bitOr:` `bitXor:` `bitShift:`, `printString: base` |
| Fractions | `numerator`, `denominator`, `reciprocal` (`3 / 4` is a `Fraction`, Chapter 3) |

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
`lib/stream.st` without any path. A module name is looked up in the current
directory first, then in the directories of `$STPATH`, then in the active venv,
and last in `lib/`, so a module of the same name in any of the earlier places
shadows the stdlib one. The directory of the importing file is not searched:
run a program that imports its own modules from the directory that holds them.

The classes a module declares become attributes of the module object *and*
globals of your program, exactly as if you had declared them yourself. That
matters when a module declares a class with the name of a kernel class, as
the older `random` and `time` modules do (§9.5, §9.7).

> **In Python** `import json` binds a module object to the name `json`, and you
> reach into it with `json.loads`. **In JavaScript**, `import * as json from
> 'json'`. **In protoST** `m := Import from: 'json'` binds the module to a
> variable *you* name, and you reach in with a *message send*: `m JSON`. The
> module is a plain object; reading a class out of it is the same dotless
> unary send you use everywhere else.

The standard modules:

| Module | Import | Provides |
|--------|--------|----------|
| `json` | `Import from: 'json'` | `JSON` — `parse:` and `stringify:` |
| `stream` | `Import from: 'stream'` | `ReadStream`, `WriteStream` — thin subclasses of the kernel classes |
| `random` | `Import from: 'random'` | `Random` — a fixed-seed LCG; replaces the kernel `Random` |
| `time` | `Import from: 'time'` | `Time`, `Timestamp`, `Duration` — replaces the kernel `Time` and `Duration` |

## 9.4 Streams — cursor-based collection access

`ReadStream` (sequential reading over a collection) and `WriteStream`
(sequential appending into a growing one) are kernel classes: no import. A
stream is a *cursor*: it remembers a position so you can consume a collection
piece by piece, or build one up.

```smalltalk
"-- stream-demo.st --"
rs := ReadStream on: #(10 20 30 40).
first := rs next.
second := rs next.

ws := WriteStream on: String new.
ws nextPutAll: 'sum: '; print: first + second.

{ first. second. rs atEnd. ws contents }.
```

```bash
$ ./build/protost stream-demo.st
#(10 20 false 'sum: 30')
```

`ReadStream on:` wraps a collection; `next` answers the element at the cursor
and advances, and answers `nil` once the stream is exhausted. `atEnd` tests
for exhaustion; `peek`, `upTo:`, `upToEnd`, `skip:` and `skipSeparators` are
there too. `WriteStream on: String new` builds a string (`on: Array new` an
array); `nextPut:` appends one element, `nextPutAll:` a collection, `print:`
an object's `printString`, and `contents` answers what was written.
`String streamContents: [ :s | … ]` is the shorthand:

```bash
$ ./build/protost -e "String streamContents: [ :s | s nextPutAll: 'n='; print: 42 ]"
n=42
```

Because strings are immutable in protoST, a `WriteStream` (or `,`) is how you
build one piece by piece.

> **In Python** a `ReadStream` is roughly an *iterator* (`next()`); a
> `WriteStream` on a string is an `io.StringIO`. **In JavaScript**, a
> generator and an array you `push` to. protoST's streams package the
> "remember where I am" idiom as a small, explicit object.

The `stream` module (`Import from: 'stream'`) predates the kernel classes and
is kept so that programs written against it keep working: `m ReadStream` and
`m WriteStream` are thin subclasses of the kernel classes.

## 9.5 `Random` — pseudo-random numbers

`Random` is a kernel class: the Park-Miller minimal standard generator of
Smalltalk-80. `Random new` is seeded from the system's entropy source, so it
differs from run to run; `Random seed: anInteger` gives a reproducible
sequence. (It is not cryptographic — do not use it for security.)

```smalltalk
"-- random-demo.st --"
r := Random seed: 42.
roll := r nextInt: 6.
{ roll between: 1 and: 6.
  (r between: 100 and: 200) between: 100 and: 200.
  (Random seed: 7) next = (Random seed: 7) next }.
```

```bash
$ ./build/protost random-demo.st
#(true true true)
```

The protocol: `next` (a `Float` in [0, 1)), `nextInt: n` (an integer in
1..n), `between: lo and: hi` (an integer in that inclusive range), and
`atRandom` on a collection or an integer (`#(1 2 3) atRandom`, `6 atRandom`).
The example checks ranges rather than printing numbers, because the numbers
themselves are not the point.

> **In Python** `random.seed(42)` then `random.randint(1, 6)`. **In
> JavaScript**, `Math.random()` — which is *not* seedable, so reproducible
> randomness needs a library. With `Random seed:`, protoST gives a
> deterministic sequence, which suits simulations and tests.

The older `random` module (`Import from: 'random'`) is a different generator:
a 32-bit linear congruential one whose `Random new` uses a fixed default seed,
with an extra `next: n` (an Array of `n` floats). Importing it **rebinds the
global `Random`** to the module's class for the rest of the program (a
module's classes are declared as globals, §9.3), so use one or the other in a
program, not both.

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
#('Ada' 3 '[1,2,3]')
```

`parse:` of the object string gives a `Dictionary`; `doc at: 'name'` is the
string `'Ada'`, `doc at: 'scores'` is a three-element `Array`. `stringify:` of
`#(1 2 3)` gives the string `'[1,2,3]'`.

> **In Python** this is `json.loads` / `json.dumps`. **In JavaScript**,
> `JSON.parse` / `JSON.stringify` — and protoST deliberately mirrors the
> JavaScript spelling, `JSON parse:` / `JSON stringify:`, because it is the
> name every web developer already knows. A JSON object becomes a
> `Dictionary`, a JSON array becomes an `Array` — the natural protoST
> counterparts.

## 9.7 Time — clocks, dates, durations

The kernel has the Smalltalk-80 classes, reading the local wall clock:

- `Time now` answers the time of day (it prints as `09:05:00`);
  `Time hour:minute:second:` builds one.
- `Date today` answers the current date, printed in ISO 8601 (`2026-10-15`);
  `Date year:month:day:` builds one, answering `year`, `monthIndex`,
  `dayOfMonth` and `monthName`.
- `Duration seconds:` / `milliseconds:` build a span; durations add, subtract
  and compare, and answer `asSeconds` / `asMilliseconds`.
- `[ … ] timeToRun` evaluates a block and answers the elapsed `Duration`
  (measured on a monotonic clock); `Time millisecondsToRun: [ … ]` answers the
  same as an integer number of milliseconds.

```smalltalk
"-- time-demo.st --"
d := Duration seconds: 90.
longer := d + (Duration seconds: 120).
elapsed := [ 1 to: 100000 do: [ :i | i * i ] ] timeToRun.
{ d asSeconds. longer asSeconds. elapsed class.
  (Date year: 2026 month: 10 day: 15) printString }.
```

```bash
$ ./build/protost time-demo.st
#(90 210 Duration '2026-10-15')
```

There are no time zones: `Time now` and `Date today` are local wall-clock
readings. A `Date` built with `year:month:day:` answers `nil` to `dayOfWeek`;
only `Date today` knows its weekday.

The older `time` module (`Import from: 'time'`) has a different model: its
`Time now` answers a `Timestamp` (epoch milliseconds) that composes with its
`Duration` (`Timestamp - Timestamp` → `Duration`, and a `Duration minutes:`
constructor). Importing it **rebinds the globals `Time` and `Duration`** to the
module's classes, as with `random` above.

## 9.8 Reading the library as protoST code

The kernel classes and the modules are ordinary protoST `.st` files, written
in exactly the language this tutorial teaches: the kernel is loaded at start-up
from `lib/kernel/` (`stream.st`, `time.st`, `fraction.st`, `collection.st`,
…), and the modules live in `lib/`. You can open them, read them, and learn
from them — they show the class-side-constructor pattern from
[Chapter 5](05-classes-and-methods.md) in real use (`ReadStream class >> on:`,
`Random class >> seed:`, `Duration class >> seconds:`).

Writing your own module is the same: create a `.st` file, declare classes, and
every non-`_`-prefixed class becomes an attribute of the module object that
`Import from:` returns.

**Printing.** `Transcript` writes to standard output, as in Smalltalk-80:
`Transcript show: 'total: '; print: 42; cr`. `printNl` (and `displayNl`, which
leaves the quotes off a string) print the receiver followed by a newline and
answer the receiver. Both write in program order.

## 9.9 Summary

- protoST's library is in two parts: the **kernel**, always loaded — the
  mathematical protocol on `Number`, streams, `Random`, `Time`, `Date`,
  `Duration`, `Transcript` — and **modules** pulled in with `Import from:`.
- The maths protocol is exact where it can be — `raisedTo:` and `factorial`
  promote to `LargeInteger`, `/` answers a `Fraction` — and total where it
  cannot — domain errors yield `nan`/`inf`, not exceptions.
- `Import from: 'name'` loads a module (cached) and answers a module object;
  read its classes with unary sends (`m JSON`). The module's classes also
  become globals.
- The standard modules: `json` (`parse:`/`stringify:`), and `stream`,
  `random` and `time`, kept from before the kernel had those classes
  (`random` and `time` replace the kernel's `Random`, `Time` and `Duration`
  when imported).
- The kernel and the modules are plain protoST `.st` files under `lib/` —
  readable, and the model for writing your own.

---

Next: [Chapter 10 — Actors and futures](10-actors-and-futures.md)
