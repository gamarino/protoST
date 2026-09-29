# Chapter 14 — For the Smalltalk programmer

[Tutorial index](../TUTORIAL.md) · Previous: [Chapter 13](13-worked-example.md)

---

This chapter is written for one reader: someone who already knows
Smalltalk-80. The other thirteen chapters teach the language; this one is a
precise, honest catalogue of every way protoST is **not** the dialect you know.
Read it and you will know exactly what to expect — what is added, what is
missing, what is changed, and what is currently broken.

The headline departures are the **actor model**, **futures**, and **cooperative
yield** — protoST's reason to exist. After those come the **object-model
extensions** (`uses:`, `addBehavior:`), the **module / venv** system, and a
catalogue of smaller intentional deviations and known limitations. Where this
chapter and the live tracker disagree, [`docs/STATUS.md`](../STATUS.md) wins —
it is verified against the build with every change.

## 14.1 The big addition: actors and futures

This is the feature protoST is *for*, and it is not in any Smalltalk-80.

**Any object becomes an actor with `asActor`.** `anObject asActor` answers an
actor proxy. A message sent to the proxy is enqueued on a mailbox, processed
one-at-a-time by a worker thread, and the send returns a `Future`
*immediately* — it does not block. Different actors run in parallel on a worker
pool. The single-message-at-a-time rule serialises access to the wrapped
object's state, so you write no locks.

**`Future` is a first-class promise.** `wait` blocks for the value (re-raising
a rejection), `thenDo:` / `catch:` register callbacks, `Future new` plus
`resolve:` / `rejectWith:` gives you a manually-settled promise.

**Cooperative yield.** A `wait` on a pending future *inside an actor method*
suspends that actor cooperatively and releases its worker thread; the actor
resumes when the future settles. This is what lets thousands of interdependent
actors run on a small thread pool. (A `wait` inside a block that a primitive
evaluates — `do:`, `collect:`, `ensure:`, … — keeps the worker instead,
running other actors meanwhile; the results are the same.) A `wait` from the *main* thread (a script
top level, the REPL) instead blocks that OS thread — the main thread is a
synchronous client of the actor world.

[Chapter 10](10-actors-and-futures.md) is the full treatment. If you have used
Erlang/Elixir processes, or Akka, the model will be familiar; the protoST
twist is that the actor is a *transparent proxy over an ordinary object* — the
object need not be written as an actor, and inside an actor method `self` is
the plain wrapped object, so a self-send is ordinary synchronous dispatch.

There is no analogue of this in Smalltalk-80's `Process` / `Semaphore` world —
protoST's actors are a higher-level model layered on protoCore's GIL-free
native threads, not green threads cooperating through semaphores.

## 14.2 The object-model extensions

protoST's kernel, protoCore, is prototype-based and supports **multiple
parents**. protoST exposes that through two features that standard Smalltalk
does not have.

**`uses:` — multiple inheritance and mixins.** A class declaration may carry a
`uses:` clause naming additional parent classes:

```smalltalk
Object subclass: #Money
  instanceVariableNames: 'cents'
  uses: { Comparable. Printable }.
```

A mixin is not a distinct entity — it is an ordinary class listed in `uses:`.
Method resolution across parents is depth-first, left-to-right: primary
superclass subtree first, then each `uses:` mixin in listed order; the diamond
case resolves to the first match; `super` follows the same order.
`subclass:uses:` and `subclass:instanceVariableNames:uses:` are also ordinary
messages on any class object, so the primary superclass may be an expression.

**`addBehavior:` — runtime behaviour composition.** `aClass addBehavior:
aMixin` composes a mixin into a class *at runtime*, no recompilation.
`addParent:` is a lower-level alias. There is no `removeBehavior:`.

[Chapter 11](11-advanced-object-model.md) covers both. Two things a Smalltalker
should note: the object model *presents* as classes (`subclass:`, `>>`,
`super`) but *is* prototype-chain delegation underneath — the metaclass is
thin (see §14.4) — and the mixin features are genuine multi-parent
inheritance, not the method-copying "trait" simulation a single-inheritance
Smalltalk would use.

## 14.3 Modules and virtual environments

Standard Smalltalk lives in an *image* — a persistent world of objects you grow
and snapshot. protoST has **no image**. It is strictly file-based:

- A `.st` **file is a module**. Loading it runs its top-level forms; the module
  object exposes the non-`_`-prefixed names it defined.
- **`Import from: 'name'`** loads a module (cached) and answers it; you read
  its classes with unary sends (`m Counter`).
- A Python-style **venv** (`protost venv create` / `activate` / `info`)
  isolates a project's modules.

[Chapter 9](09-standard-library.md) and [Chapter 12](12-tooling.md) cover
these. The consequence for a Smalltalker: there is no `ChangeSet`, no
`fileOut`, no world snapshot, no live-image workflow. You edit `.st` files in a
text editor and run them — the model is "a language with source files", like
Python or a compiled language, not "a living image". The file-out *syntax*
(`ClassName >> selector`) is familiar; the *persistence model* is not.

## 14.4 Deviations from Smalltalk-80

Every entry below was checked against the 0.4.0 build. Each says what differs,
why, and what to write instead. The ids (`D2`, `D33`, …) are those of
[`docs/STATUS.md`](../STATUS.md), the live tracker.

### Programs are files, not an image

protoST runs plain `.st` files (§14.3). There is no image, no browser, no
`ChangeSet`, no `become:` and no persistence of the object world between runs.
Classes are declared with `Superclass subclass: #Name instanceVariableNames:
'…' classVariableNames: '…'` and methods with `Name >> selector` followed by
the body (`Name class >> selector` for the class side).

### A blank line ends a method body — D33

Without chunk separators, the blank line is the separator. The body of a
method ends at the next blank line or the next declaration, whether or not its
last statement has a period, and a statement may not span a blank line
outside parentheses, brackets or braces. Write methods without blank lines
inside them. If a top-level statement is swallowed by the method above it,
the compiler's "undeclared variable" message says so.

### A script shows the value of its last statement — D12, D12b

A script has no entry point: its top-level forms run in order and the CLI
prints the value of the last statement (its `displayString`), the way a
workspace's *print it* would. A script that ends with `x printNl.` therefore
shows `x` twice; end it with an expression, or with `nil`, if that matters.
Top-level `| a b |` temporaries are accepted, as in a workspace.

### Strings are immutable — D34

`'abc' copy at: 1 put: $x` signals `ModificationForbidden`. protoCore's
collections are immutable values shared by structure, which is what makes
passing them to actors on other threads safe without copies or locks. Build
strings with `WriteStream on: String new` (or `String new writeStream`), `,`,
`copyReplaceAll:with:`, `copyReplacing:with:` or `String new: 5 withAll: $x`.

### Short symbols are strings — D35

A Symbol shorter than 8 bytes is represented exactly like the equal String:
`#foo == 'foo'` is `true` and `#at: printString` is `'at:'`. Longer symbols are
distinct objects. Equality, hashing and dictionary keys behave as you expect
(in Smalltalk a Symbol is equal to the equal String too); only identity tests
and the printed form differ.

### A few names and printed forms — D36

`aClass name` answers a String. Large integers have one class, `LargeInteger`
(Pharo splits it by sign). `Date today printString` is ISO 8601
(`2026-09-29`), `[…] timeToRun` answers a `Duration` printed as Pharo prints
it (`0:00:00:01.500`).

### The metaclass is thin

`3 class class` is `SmallInteger class` and class-side methods, class
variables and class-instance variables (`Foo class instanceVariableNames:
'default'`) all work. There is no full `Metaclass`/`Behavior`/`ClassDescription`
hierarchy to program against: the reflective protocol is the one listed in
§14.6 (`instVarNames`, `selectors`, `canUnderstand:`, `subclasses`, …).

### `thisContext` is reserved but inert — D17

It parses, but the context protocol is not built. Use the error traces
(`at Class>>selector (file:line)`) and the debugger ([Chapter 12](12-tooling.md)).

### `outer` is an alias of `pass` — D7

`outer` continues the handler search outward and does not return to the inner
handler. `pass`, `retry`, `retryUsing:`, `resume:`, `return:` and `signal`
behave as in Pharo.

### An actor that waits is not re-entrant — D37

While an actor's method is parked on `wait`, that actor processes no other
message, so two actors that `wait` on each other could never proceed; the
`wait` that would close such a cycle signals an `Error` ("deadlock: …")
instead of hanging. This is the price
of the guarantee that makes actors simple: one message at a time, so an
actor's state is plain instance variables with no locks. Inside actors, chain
with `thenDo:` / `catch:` or `Future whenAll:` / `whenAny:` rather than waiting
on an actor that may call back. ([Chapter 10](10-actors-and-futures.md).)

### Recursion depth is bounded — D38

A method recursion stops at about 19,000 activations, and a recursion that
goes through native iteration (`do:`, `collect:`, …) at about 1,000 levels,
with a catchable `Error` ("stack depth exceeded"); `ensure:` blocks still run.

### Arity limits

A message send carries at most 8 arguments. Blocks are called with `value`
… `value:value:value:value:` or, for any arity, `valueWithArguments:`.

### Single `STRuntime` per process — D2

A protoST runtime must be the only one in its process. The `protost` CLI always
constructs exactly one, so this matters only when you embed protoST.

### `addBehavior:` affects future instances only — D21

`aClass addBehavior: aMixin` reaches the class and the instances created after
the call; existing instances keep their parent chain (protoCore fixes it at
construction). Methods installed with `>>` are seen by existing instances.
Call `addBehavior:` during setup. ([Chapter 11](11-advanced-object-model.md).)

## 14.5 What was different before 0.4.0 and no longer is

If you read older protoST material, these are now as in Pharo:

- `new` sends `initialize`; `basicNew` is the raw allocator. Built-in
  collection classes do the same for your subclasses (`OrderedCollection
  subclass: #Stack`, then `Stack new` is a `Stack`).
- `Transcript` exists (`show:`, `cr`, `print:`, `display:`, `tab`, `space`,
  `showCr:`, `<<`) and keeps order with `printNl`.
- `Character` exists (`$a`, `Character value: 65`, `isVowel`, `asUppercase`,
  `asInteger`); `String at:` answers a Character.
- `/` between integers is exact: `3 / 4` is the Fraction `(3/4)`; `//`, `\\`,
  `quo:`, `rem:` follow Smalltalk-80 (floor and truncation). Fractions take
  part in every arithmetic and comparison, and `ZeroDivide` (an
  `ArithmeticError`) is resumable.
- Class variables may be assigned from instance methods, and class-instance
  variables exist.
- `doesNotUnderstand:` can be overridden (it receives a `Message`), and
  `perform:`, `respondsTo:`, `isKindOf:` work.
- Every block activation has its own variables: blocks created in a loop
  capture that iteration's value, and a block parameter named like an outer
  variable is a distinct variable.
- Errors carry their class: `SubscriptOutOfBounds`, `KeyNotFound`,
  `NotFound`, `ZeroDivide`, `MessageNotUnderstood`; an error inside an actor
  keeps its class when a `wait` re-signals it; an uncaught error prints its
  class, text and a trace.
- The compiler reports, as Pharo does: an undeclared variable in a method,
  assignment to an argument or to `self`, a duplicated or redeclared instance
  variable.

## 14.6 Protocol you can rely on

Beyond the kernel classes you know, 0.4.0 answers the everyday protocol a
Smalltalk programmer reaches for:

- **Numbers:** exact Fractions, LargeIntegers, `printString:` / `printStringRadix:`,
  radix literals (`16rFF`), `gcd:`, `lcm:`, `factorial`, `sqrt`, `raisedTo:`,
  `**`, `roundTo:`, `bitAnd:` / `bitOr:` / `bitXor:` / `bitShift:`, `min:` /
  `max:` / `between:and:` across the tower.
- **Collections:** `with:`… / `withAll:` / `new:` constructors, `collect:`,
  `select:`, `reject:`, `detect:ifNone:`, `inject:into:`, `do:separatedBy:`,
  `allSatisfy:` / `anySatisfy:`, `groupedBy:`, `detectMax:`, `sum` / `sum:`,
  `asSortedCollection:`, `sort:`, `SortedCollection`, `Bag`, `Set`,
  `Dictionary` (`at:ifAbsent:`, `at:ifPresent:`, `at:ifAbsentPut:`,
  `keysAndValuesDo:`, `collect:` / `select:` answering Dictionaries),
  `Interval` (`1 to: 10 by: 2`), `Association`. A `Collection` subclass that
  defines `do:` gets all of this.
- **Strings and streams:** `,`, `copyFrom:to:`, `indexOf:`, `occurrencesOf:`,
  `substrings:`, `lines`, `trimBoth`, `asUppercase`, `format:`, `beginsWith:`,
  `includesSubstring:`, `asNumber`, `asSymbol`; `ReadStream` (`next`, `peek`,
  `upTo:`, `skipSeparators`, …), `WriteStream`, `String streamContents:`.
- **Exceptions:** `on:do:`, `ensure:`, `ifCurtailed:`, `signal:`, `retry`,
  `retryUsing:`, `resume:`, `return:`, `pass`, `ExceptionSet` (`Error ,
  ZeroDivide`), user `Error` subclasses.
- **Reflection:** `class`, `superclass`, `respondsTo:`, `perform:`…,
  `instVarNames`, `instVarNamed:`, `instVarAt:`, `canUnderstand:`,
  `selectors`, `subclasses`, `comment:`, `deepCopy`, `inspect`.
- **System:** `Smalltalk at:` / `at:put:` / `version` / `allClasses`, `Time
  now`, `Date today`, `Time millisecondsToRun:`, `timeToRun`, `Random` (`next`,
  `nextInt:`, `seed:`), `halt` (stops under `protost -d`, reported otherwise).

## 14.7 Guard clauses and the trailing `^`

The *guard-clause* style — an early `^` inside an `ifTrue:` block, followed by
the method's main body — works as written:

```smalltalk
"guard-clause style — works"
Account >> withdraw: amount
  amount > balance ifTrue: [ ^ self error: 'insufficient funds' ].
  balance := balance - amount.
  ^ balance.
```

> A guard-clause `^` returning a bare instance variable used to be mis-compiled
> (the variable was wrongly boxed into a closure). That was tracker item D22,
> closed — the guard-clause form is now reliable. See `docs/STATUS.md`.

The **expression form** — compute the whole result and `^` it once — is equally
valid and many Smalltalkers prefer it stylistically:

```smalltalk
"expression form — also valid"
Account >> withdraw: amount
  ^ (amount > balance)
      ifTrue:  [ InsufficientFunds signal: 'insufficient funds' ]
      ifFalse: [ balance := balance - amount. balance ].
```

One rule of the file syntax applies here: the **first *top-level* `^`
terminates the method body**, like a blank line does (see "Where a method body
ends" in [Chapter 5](05-classes-and-methods.md) §5.3). A `^` *nested in a
block* — as in the guard clause above — does not terminate the method; only a
`^` written as a top-level statement does, so anything written after it in
the same method is read as top-level code.

## 14.8 What is unchanged — the Smalltalk you keep

Lest this chapter read as a list of losses: the *core* of Smalltalk-80 is
intact and faithful. You keep, unchanged:

- The syntax — unary / binary / keyword messages, the three-level precedence,
  cascades `;`, `.` statement terminators, the file-out `>>` method form.
- "Everything is an object, everything is a message" — no operators that are
  not messages, no control-flow keywords.
- Blocks as first-class closures, with capture by reference.
- **Non-local return** — `^` from inside a block returns from the home method,
  from any nesting depth. Standard semantics.
- The exception protocol — `signal` / `on:do:` / `ensure:` / `ifCurtailed:`,
  and the handler actions `return:` / `retry` / `resume:` / `pass`.
- The collection hierarchy and its uniform iteration protocol — `do:`,
  `collect:`, `select:`, `reject:`, `detect:`, `inject:into:`, and the rest.
- `super`, instance variables, the prototype-presented-as-class object model.

If you skim Chapters 2–9 you will recognise nearly everything. The genuinely
new material is Chapters 10 (actors/futures) and 11 (`uses:` / `addBehavior:`),
plus the deviations catalogued above. protoST's stated design stance is "as
close and as compliant as reasonable to Smalltalk-80, but standard conformance
is not the goal" — the goal is a coherent language that shows off the protoCore
kernel. This chapter is the precise measure of that "reasonable".

## 14.8b Call-form sends — a protoCore-shaped extension

protoST adds a *call-form* message syntax that has no equivalent in
Smalltalk-80:

```smalltalk
Counter >> incr(by, factor = 1)   "declaration"
    value := value + (by * factor).
    ^ value.

c incr(2, factor = 3)              "call site"
```

It binds at unary precedence and accepts a comma-separated list of
positional arguments followed by named arguments of the form `name =
value`. Named parameters in declarations carry default expressions
evaluated lazily at call time in the method's own scope.

This shape exists because protoST shares the protoCore runtime kernel
with protoPython, protoJS, and other protoCore-hosted languages. Those
runtimes name methods with plain identifiers and pass positional + named
arguments. The call-form syntax is meant to let protoST call such methods
without selector-mangling at the bridge layer; in 0.4.0 it reaches protoST
methods only, and calling a foreign runtime's method this way is not
implemented yet ([`INTEROP.md`](../INTEROP.md) §0, §3.5).

Call-form methods and keyword-form methods are **distinct attributes**: a
class may host both `>> bar(x)` and `>> bar: x` without conflict. The
call-form method registers on the class under the bare name. Side-effect
order: positional args evaluate in source order; **named args evaluate
in alphabetical-key order** — explicitly different from Python, which
guarantees left-to-right. Use explicit temps where it matters.

A class cannot host *both* a unary `>> bar` and a call `>> bar(...)`
since they share the bare-name attribute key. Pick one form per name.

In v1, actor receivers reject call-form sends — the async message
envelope does not yet decode the positional + named shape. Keyword
sends remain the way to message an actor.

For full grammar, see `LANGUAGE.md` §3.5.1 and §3.3.

## 14.9 Summary

- **Added, not in Smalltalk-80:** the actor model (`asActor`), futures
  (`wait` / `thenDo:`), cooperative yield; `uses:` multiple inheritance /
  mixins; `addBehavior:` runtime composition; file-based modules and venvs;
  **call-form sends** (`recv name(p, k = v)`) and call-form method
  declarations (`Class >> name(p, k = default)`).
- **Intentional deviations (§14.4):** programs are files, not an image; a
  blank line ends a method body (D33); a script shows its last value (D12,
  D12b); strings are immutable (D34); short symbols are represented as strings
  (D35); a few printed forms differ (D36); a thin metaclass; an actor that
  waits is not re-entrant (D37); bounded recursion depth (D38); `outer`
  aliases `pass` (D7); single runtime per process (D2); `addBehavior:` affects
  future instances only (D21).
- **Not implemented:** `thisContext` is inert (D17).
- **As in Pharo since 0.4.0 (§14.5):** `new` sends `initialize`; `Transcript`;
  `Character`; exact `Fraction` division with floored `//` and `\\`; class
  variables and class-instance variables; `doesNotUnderstand:` overrides;
  per-activation block variables; exceptions that keep their class; top-level
  `| temps |`.
- **Unchanged:** the syntax, the message model, blocks and closures, non-local
  return, the exception protocol, the collection protocol, `super`, the
  object model.
- `docs/STATUS.md` is the live, build-verified tracker — consult it when in
  doubt.

---

[Tutorial index](../TUTORIAL.md)
