# Chapter 4 — Blocks

[Tutorial index](../TUTORIAL.md) · Previous: [Chapter 3](03-variables-and-literals.md) · Next: [Chapter 5 — Classes, objects, methods](05-classes-and-methods.md)

---

[Chapter 2](02-objects-and-messages.md) ended with a promise: protoST has no
control-flow keywords because conditionals and loops are *messages that take
blocks as arguments*. This chapter delivers on it. A block is the single most
important object type in the language after the message send itself. Master
blocks and protoST control flow is yours.

## 4.1 What a block is

A **block** is a chunk of code wrapped up as an object — a piece of behaviour
you can store in a variable, pass as an argument, and run later, possibly more
than once, possibly never. Written between square brackets:

```smalltalk
[ 3 + 4 ]
```

That expression does **not** compute `7`. It produces a `Block` object — an
instance of the class `Block` — whose body is the code `3 + 4`. The body runs
only when you *evaluate* the block by sending it the message `value`:

```bash
$ ./build/protost -e '[ 3 + 4 ] value'
7
```

`[ 3 + 4 ]` builds the block; `value` runs it. Two separate steps.

> **In Python** a block is a `lambda`: `lambda: 3 + 4` builds a callable, and
> `(lambda: 3 + 4)()` runs it. **In JavaScript** it is an arrow function:
> `() => 3 + 4`, run with `()`. **In protoST** a block is `[ 3 + 4 ]`, run with
> `value`. Same idea exactly — deferred code as a first-class value — but with
> one crucial difference from a Python `lambda`: a Python lambda's body is a
> *single expression*, whereas a protoST block's body is a full *sequence of
> statements*. A block is closer to a JavaScript arrow function with a
> `{ … }` body, or to a Python *nested function*, than to a one-line lambda.

## 4.2 Blocks with arguments

A block can declare arguments. Each argument is `:name`, the list of them is
terminated by `|`, and the body follows:

```smalltalk
[ :x | x * x ]
```

This is a one-argument block. You evaluate a one-argument block by sending it
`value:` with the argument:

```bash
$ ./build/protost -e '[ :x | x * x ] value: 7'
49
```

The arity of the `value` message must match the number of arguments the block
declares. There is a distinct selector for each arity:

| Block | Evaluate with | Example |
|-------|---------------|---------|
| `[ 42 ]` | `value` | `[ 42 ] value` → `42` |
| `[ :x | … ]` | `value:` | `[ :x | x + 1 ] value: 9` → `10` |
| `[ :a :b | … ]` | `value:value:` | `[ :a :b | a + b ] value: 3 value: 4` → `7` |
| `[ :a :b :c | … ]` | `value:value:value:` | three arguments |
| `[ :a :b :c :d | … ]` | `value:value:value:value:` | four arguments |

```bash
$ ./build/protost -e '[ :a :b | a + b ] value: 3 value: 4'
7
```

Evaluating a block with the *wrong* number of arguments is a runtime error —
there is no currying and no default arguments. A block of arity 0–4 is
supported; if you need more inputs, pass a collection.

> **In JavaScript** `(a, b) => a + b` is the analogue, called as `f(3, 4)`.
> **In protoST** the arguments are *interleaved* into the selector exactly as
> they are for keyword messages: `value: 3 value: 4`. The block does not have
> a "call" syntax of its own — it is just an object, and you run it by sending
> it an ordinary keyword message whose name happens to be `value:value:`.

## 4.3 Block-local variables

A block may declare its own temporary variables, with the same `| names |`
syntax a method uses. They go *after* the argument list and *before* the body:

```smalltalk
[ :x | | doubled | doubled := x * 2. doubled + 1 ]
```

Here `:x` is an argument and `doubled` is a block-local temporary. Each
evaluation of the block gets a fresh `doubled`.

```bash
$ ./build/protost -e '[ :x | | t | t := x * 2. t + 1 ] value: 10'
21
```

The same caveat from [Chapter 3](03-variables-and-literals.md) applies, with a
twist worth stating: `| temps |` is legal inside a *block* and inside a
*method*, but **not at the top level of a script**. A block written at the top
level may declare locals, but the script's bare top-level statements may not.

## 4.4 The value of a block

A block evaluates to the value of its **last statement**:

```bash
$ ./build/protost -e '[ 1. 2. 3 ] value'
3
```

The body `1. 2. 3` is three statements; the block's value is the last one, `3`.
An empty block `[ ]` evaluates to `nil`.

> **In Python** a `lambda` returns its single expression; a nested function
> returns whatever it `return`s, or `None`. **In JavaScript** an arrow function
> with a `{ … }` body must `return` explicitly. **In protoST** a block has no
> `return` — it simply *yields its last statement's value*. (There is a `^`
> form, but as [Chapter 6](06-non-local-return.md) explains, `^` does something
> much more dramatic than "return from the block": it returns from the whole
> enclosing *method*. For ordinary "the block's result is its last line", you
> write no `^` at all.)

## 4.5 Blocks are closures

This is the property that makes blocks powerful rather than merely convenient.
A block **captures the variables of the scope it is written in**. It does not
copy their values — it captures the variables themselves, by reference.

Consider a counter built entirely from a block and a captured variable:

```smalltalk
"-- closure.st --"
count := 0.
bump := [ count := count + 1 ].
bump value.
bump value.
bump value.
count.
```

```bash
$ ./build/protost closure.st
3
```

The block `[ count := count + 1 ]` was written where `count` is in scope, so
it captured `count`. Every time the block runs, it reads and writes *that same*
`count`. After three evaluations `count` is `3`. The variable outlived the
statement that created the block, and the block kept it alive — that is a true
closure.

Capture works through nesting: a block written inside another block sees the
outer block's variables; a block written inside a method sees that method's
arguments, temporaries, instance variables, and `self`. We will rely on this
constantly from [Chapter 5](05-classes-and-methods.md) onward.

> **In Python** a nested function closes over the enclosing scope, but writing
> a captured variable needs the `nonlocal` keyword — `count += 1` inside a
> nested function fails without `nonlocal count`. **In JavaScript** a closure
> can read *and write* a captured `let`/`var` freely. **In protoST** it is the
> JavaScript behaviour: a block reads and writes captured variables with no
> declaration ceremony. `[ count := count + 1 ]` just works, and the change is
> visible to everyone else holding that variable.

> **Limitation — shadowing.** A block cannot declare a local (or argument) with
> the *same name* as a variable it captures from the enclosing method. The
> capture mechanism uses one flat per-method table and cannot keep two
> same-named variables apart. In practice this is never a constraint worth
> worrying about — give the inner variable a different name — but it is a
> documented edge of the language. (`docs/STATUS.md` records this as resolved
> on the current build for the common cases; the safe habit is simply not to
> shadow.)

## 4.6 Control flow is blocks plus messages

Now the payoff. Because a block is "code I can hand to someone else to run
later, maybe conditionally, maybe repeatedly", every control structure you know
from other languages becomes an ordinary message that takes blocks.

### Conditionals — messages on a Boolean

`x > 0` produces a boolean object. You send that boolean a conditional message,
passing blocks for the branches:

```bash
$ ./build/protost -e '(10 > 3) ifTrue: [ 'bigger' ] ifFalse: [ 'smaller' ]'
bigger
```

The boolean `true` has an `ifTrue:ifFalse:` method that evaluates its first
block and ignores the second; `false` does the opposite. The branches are
blocks precisely so that only the chosen one runs — handing both branches as
*already-computed values* would run both, which is rarely what you want.

The full conditional protocol on `Boolean`:

| Message | Effect |
|---------|--------|
| `ifTrue:` | evaluate the block when the receiver is `true`, else answer `nil` |
| `ifFalse:` | evaluate the block when the receiver is `false`, else answer `nil` |
| `ifTrue:ifFalse:` | two-armed conditional — exactly one block runs |
| `ifFalse:ifTrue:` | the same, branches written in the other order |
| `and:` | the argument is a *block*; evaluated only if the receiver is `true` |
| `or:` | the argument is a *block*; evaluated only if the receiver is `false` |
| `&` `|` `xor:` | eager combinators — the argument is an already-computed boolean |
| `not` | logical negation |

The difference between `and:` and `&` is the difference between Python's `and`
and a non-short-circuiting bitwise `&`. `and:` takes a **block** and evaluates
it only when needed:

```bash
$ ./build/protost -e '(3 > 5) and: [ 1 / 0 ]'
false
```

The receiver `3 > 5` is `false`, so `and:` never evaluates its block — and the
`1 / 0` that would have divided by zero is never reached. That is short-circuit
evaluation, and it works only because the right-hand side is a *block*, not a
value. `&`, by contrast, takes a plain boolean and always evaluates both sides.

```bash
$ ./build/protost -e '(3 > 2) and: [ 5 > 4 ]'
true
```

> **In Python/JS** short-circuiting is built into the `and`/`or`/`&&`/`||`
> operators by the grammar. **In protoST** there is no grammar magic: `and:`
> takes a block and chooses whether to evaluate it; `&` takes a value and does
> not. The short-circuit *is* the deferred block. Once you see that, you see
> why protoST needs no special operator — laziness is just "pass a block".

### Loops — messages on a Block

A `while` loop is a message sent to a block. The receiver block is the
condition; the argument block is the body:

```smalltalk
"-- loop.st --"
i := 0.
sum := 0.
[ i < 5 ] whileTrue: [ i := i + 1. sum := sum + i ].
sum.
```

```bash
$ ./build/protost loop.st
15
```

`whileTrue:` evaluates the receiver block; while it answers `true`, it
evaluates the argument block, then repeats. `whileFalse:` is the mirror image.
There is also `repeat` — an unbounded loop on a block, exited only by a
non-local return ([Chapter 6](06-non-local-return.md)).

> **In Python/JS** `while (cond) { body }` is a statement with the condition
> baked into the syntax. **In protoST** *both* the condition and the body are
> blocks, and `whileTrue:` is an ordinary method on `Block`. The condition
> must be a block — not a plain boolean — because it has to be re-evaluated
> on every pass. A plain boolean would be computed once and never change.

### Numeric iteration — messages on a Number

Counting loops are messages on a number:

```smalltalk
"-- count-loop.st --"
sum := 0.
1 to: 5 do: [ :i | sum := sum + i ].
sum.
```

```bash
$ ./build/protost count-loop.st
15
```

`to:do:` is a keyword message on the integer `1`: the limit is `5`, the body is
a one-argument block that receives each integer in turn. `to:by:do:` adds a
step, and a negative step counts down:

```smalltalk
"-- countdown.st --"
sum := 0.
10 to: 1 by: -2 do: [ :i | sum := sum + i ].
sum.
```

```bash
$ ./build/protost countdown.st
30
```

The block here runs for `i` = 10, 8, 6, 4, 2, so `sum` ends at `30`. Note this
is a *script* — multi-statement code with a top-level variable belongs in a
file (or the REPL), never behind `-e`, which evaluates exactly one expression.

> **In Python** this is `for i in range(1, 6): …`. **In JavaScript**,
> `for (let i = 1; i <= 5; i++) …`. **In protoST** `1 to: 5 do: [ :i | … ]` —
> a message `to:do:` to the number `1`, the loop variable arriving as the
> block's argument. The loop is not a statement; it is an expression with a
> value (`to:do:` answers its receiver).

### Iterating a collection — a message on the collection

The same pattern reaches collections. Every collection understands `do:`, which
evaluates a block once per element:

```smalltalk
"-- sum-array.st --"
sum := 0.
#(10 20 30) do: [ :e | sum := sum + e ].
sum.
```

```bash
$ ./build/protost sum-array.st
60
```

[Chapter 8](08-collections.md) develops the full collection-iteration protocol
— `collect:`, `select:`, `inject:into:`, and the rest — but the shape never
changes: an iteration is *a message to the collection, taking a block*.

## 4.7 A block is a genuine object

Because a block is an object, you can do everything to it that you can do to
any object: store it, put it in a collection, return it from a method, pass it
around. A dictionary of blocks is a perfectly idiomatic dispatch table:

```smalltalk
"-- dispatch.st --"
ops := Dictionary new.
ops at: #double put: [ :n | n * 2 ].
ops at: #square put: [ :n | n * n ].
(ops at: #square) value: 9.
```

```bash
$ ./build/protost dispatch.st
81
```

`ops at: #square` retrieves a block; `value: 9` runs it. This is the protoST
equivalent of a Python dict of lambdas or a JavaScript object of arrow
functions — and it needs no special language support, because a block was an
object all along.

## 4.8 Summary

- A block `[ … ]` is a first-class object — deferred code as a value. It is
  protoST's lambda / arrow function / nested function.
- `[ … ]` *builds* a block; `value` / `value:` / `value:value:` … *runs* it.
  The `value` arity must match the block's argument count (0–4).
- A block's value is its last statement (`nil` if empty). To return from the
  enclosing *method*, use `^` — see [Chapter 6](06-non-local-return.md).
- Blocks are closures: they capture the variables of their defining scope by
  reference, and can read and write them.
- Control flow is blocks plus messages. `ifTrue:ifFalse:` is a message on a
  `Boolean`; `whileTrue:` and `repeat` on a `Block`; `to:do:` on a `Number`;
  `do:` on a `Collection`. `and:` / `or:` short-circuit *because* their
  argument is a block.

---

Next: [Chapter 5 — Classes, objects, methods](05-classes-and-methods.md)
