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
actors run on a small thread pool. A `wait` from the *main* thread (a script
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
`super`) but *is* prototype-chain delegation underneath — there is no metaclass
tower (see §14.4) — and the mixin features are genuine multi-parent
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

## 14.4 Intentional deviations from Smalltalk-80

These are deliberate design decisions. protoST diverges *on purpose*; the items
are not bugs and they stay. The canonical list is `docs/STATUS.md` §*Intentional
deviations*; the id scheme (`D2`, `D4`, …) is shared with that file.

### No image, no persistence

protoST is file-based (§14.3). No `ChangeSet`, no `become:`, no world snapshot,
no image save/restore.

### No metaclass tower

Class-side methods *exist* — `ClassName class >> selector` — but there is **no
separate `Metaclass` object** and no `Class class class` recursion. A class is
an ordinary prototype object. Class-side and instance-side protocols are kept
disjoint (a class-side selector sent to an instance is a
`MessageNotUnderstood`, and vice versa is allowed), but the rich metaclass
reflection of Smalltalk-80 is simply not there. If your code reasons about
metaclasses, it will not port.

### `new` does not auto-invoke `initialize` — D4

In Smalltalk-80, `Object class>>new` is conventionally `super new initialize`.
**In protoST, `new` (and its synonym `newChild`) is the raw allocator only** —
it returns an instance with `nil` fields and does *not* send `initialize`. The
caller must send `initialize` explicitly, or — idiomatically — the class
provides a class-side constructor that does both:

```smalltalk
Counter class >> new
  | c |
  c := super new.
  c initialize.
  ^ c.
```

This is the single deviation most likely to bite a Smalltalker on day one:
`Foo new` gives you an uninitialised object. `docs/STATUS.md` notes protoST
*may* adopt the Smalltalk-80 convention later; today the explicit two-step is
the contract. ([Chapter 5](05-classes-and-methods.md) §5.2.)

### `outer` is an alias of `pass` — D7

In the exception protocol, `outer` is intended to run the enclosing handler and
then *return to the inner* handler. protoST's `outer` is currently an **alias of
`pass`** — it continues the handler search outward and does not round-trip
back. True `outer` semantics need resumable handler re-entry that is not built.
`pass` covers the common case. ([Chapter 7](07-exceptions.md) §7.4.)

### No `main:` auto-invocation — D12

A protoST script has **no entry point**. It is simply its top-level forms run
in source order, and the value of the **last top-level statement** is the
program's result. There is no `main:` method that the runtime seeks out and
calls. This is deliberate CLI semantics ([Chapter 1](01-introduction.md)).

### Single `STRuntime` per process — D2

A protoST runtime must be the only one in its OS process; a second corrupts
protoCore's per-`ProtoSpace` symbol-interning caches. The `protost` CLI always
constructs exactly one, so this only matters if you *embed* protoST. ([Chapter
12](12-tooling.md) §12.5.) STATUS.md classes this as borderline-intentional: if
protoCore ever makes the caches per-space-instance, the item closes.

### `addBehavior:` affects future instances only — D21

`aClass addBehavior: aMixin` makes the class object and every instance created
*after* the call respond to the mixin's methods. An instance created *before*
the call does **not** gain them — protoCore freezes an object's parent chain
into its base cell at construction. (Methods installed directly with `>>` *are*
seen by pre-existing instances; only new *parents* are not.) The practical
guidance is to call `addBehavior:` during setup, before the affected instances
exist. ([Chapter 11](11-advanced-object-model.md) §11.3.)

## 14.5 Smaller departures and missing pieces

These are not headline design decisions, but a Smalltalker will notice them.

### No `Character` class

protoST has **no distinct `Character` type**. A character literal `$a`, and
indexing into a string (`'hello' at: 1`), both produce a **one-character
`String`**. Convert between a character and its code point with
`Number>>asCharacter` and `String>>asInteger`. Code that relies on `Character`
being its own class — `$a asUppercase`, `Character value: 65` — will not port
as written. ([Chapter 3](03-variables-and-literals.md) §3.7.)

### No `Transcript` — D10

Smalltalk-80's standard output-stream object `Transcript` is **not provided**.
`Transcript show:`, `Transcript cr` do not work. Use **`printNl`** — it prints
the receiver followed by a newline and answers the receiver. (Tracked as a
not-yet-implemented feature, owned by the standard-library track.)

### Integer `/` is truncating

Between two integers, `/` is **truncating integer division**: `10 / 4` is `2`,
`1 / 3` is `0`. protoST has **no `Fraction` type** — it follows protoCore's
integer `/`. `//` is the explicit integer-division selector. If *either*
operand is a `Float`, `/` is float division (`1 / 2.0` is `0.5`). A
Smalltalk-80 programmer expects `3 / 2` to be the fraction `3/2`; in protoST it
is `1`. ([Chapter 3](03-variables-and-literals.md) §3.4.)

### `thisContext` is reserved but inert — D17

`thisContext` parses to its own node but the reflective context protocol is
unbuilt — using it errors with `expression kind not yet supported`. Treat it as
reserved but not yet meaningful. (Not-yet-implemented; owned by the
object-model / reflection track.)

### Class variables are not implemented — D19

A non-empty `classVariableNames:` clause is **rejected with a compile-time
diagnostic** rather than honoured — per-class shared variables are not yet
built. An empty `classVariableNames: ''` clause is a documented no-op. (Tracked
as a not-yet-implemented feature.)

### The reflective `doesNotUnderstand:` hook is absent

An unresolved selector signals a catchable `MessageNotUnderstood` (a subclass
of `Error`) — see §14.6 — but the *user-overridable* `doesNotUnderstand:`
method, the proxy/metaprogramming hook a Smalltalker reaches for, is **not
implemented**. An unresolved send always signals `MessageNotUnderstood`
directly; you cannot intercept it by overriding `doesNotUnderstand:`.

### Scripts cannot declare top-level temporaries

`| temps |` is legal inside a method or a block, but **not at the top level of
a `.st` script**. At a script's top level you simply assign to a name and it
becomes a global. This is a parsing rule worth knowing because several
reference examples gloss over it. ([Chapter 3](03-variables-and-literals.md)
§3.2.) The REPL and `-e` likewise do not accept a top-level `| temps |`
declaration.

### Block argument arity is capped, and a single send carries at most 8 arguments

Blocks of arity 0–4 are supported (`value` … `value:value:value:value:`). A
single message send carries at most 8 arguments. Neither cap is a problem in
practice, but both are limits a Smalltalk-80 program does not have.

## 14.6 What is *fixed* — closed deviations a Smalltalker can rely on

Several rough edges from earlier protoST builds are resolved. They are listed
here so you do *not* worry about them — they behave correctly now.

- **`doesNotUnderstand` is catchable.** An unresolved selector signals a
  catchable `MessageNotUnderstood` (a subclass of `Error`), not a hard crash.
  `[ 3 fooBar ] on: Error do: [ :e | e messageText ]` catches it. (STATUS.md
  D3.)
- **Dead-home non-local return is catchable.** A `^` in a block whose home
  method has already returned signals a catchable `BlockCannotReturn` (a
  subclass of `Error`), not a crash. ([Chapter 6](06-non-local-return.md) §6.4;
  STATUS.md D8.)
- **The numeric tower works.** `Float` and mixed-mode arithmetic, and
  `LargeInteger` with transparent overflow promotion, all work — arithmetic is
  bound once on `Number` and delegates to protoCore's promoting/coercing
  arithmetic. `100 factorial` is exact. (STATUS.md D11, D20.)
- **Negative numeric literals lex.** `-5`, `-3.14`, `#(-1 -2 -3)` are literals;
  `a - 5` stays subtraction. (STATUS.md D1.)
- **Nested literal arrays parse.** `#(1 #(2 3) 4)` is a three-element array
  with a sub-array. (STATUS.md D16.)
- **`==` / `~~` are universal.** Identity and non-identity are bound on
  `Object`; `=` / `~=` default to identity, overridden to value equality on
  `SmallInteger`, `String`, `Symbol`, `Boolean`. Symbols are interned, so
  `#foo == #foo`. (STATUS.md D18.)
- **Class-side methods are isolated** from instances (STATUS.md D5), and
  **chained assignment** `a := b := 0` parses (STATUS.md C1).

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

One genuine rule still applies, and it is a deliberate parser characteristic,
not a bug: the **first *top-level* `^` terminates the method body** (the
`^`-terminator rule of [Chapter 5](05-classes-and-methods.md) §5.3). A `^`
*nested in a block* — as in the guard clause above — does not terminate the
method; only a `^` written as a top-level statement does. The safe universal
habit remains: end every method with a single explicit top-level `^`.

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

## 14.9 Summary

- **Added, not in Smalltalk-80:** the actor model (`asActor`), futures
  (`wait` / `thenDo:`), cooperative yield; `uses:` multiple inheritance /
  mixins; `addBehavior:` runtime composition; file-based modules and venvs.
- **Intentional deviations:** no image / persistence; no metaclass tower; `new`
  does not auto-`initialize` (D4); `outer` aliases `pass` (D7); no `main:`
  (D12); single runtime per process (D2); `addBehavior:` affects future
  instances only (D21).
- **Smaller departures / missing pieces:** no `Character` class; no
  `Transcript` (D10); truncating integer `/`; `thisContext` inert (D17); no
  class variables (D19); no user `doesNotUnderstand:` hook; no top-level script
  temporaries.
- **Fixed and reliable:** catchable `doesNotUnderstand` and dead-home return;
  the full numeric tower; negative literals; nested literal arrays; universal
  `==`/`=`.
- **Unchanged:** the syntax, the message model, blocks and closures, non-local
  return, the exception protocol, the collection protocol, `super`, the
  object model.
- `docs/STATUS.md` is the live, build-verified tracker — consult it when in
  doubt.

---

[Tutorial index](../TUTORIAL.md)
