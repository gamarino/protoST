# Chapter 6 — Non-local return

[Tutorial index](../TUTORIAL.md) · Previous: [Chapter 5](05-classes-and-methods.md) · Next: [Chapter 7 — Exceptions](07-exceptions.md)

---

This is a short chapter about a single character — `^` — and the one piece of
its behaviour that surprises every newcomer. You have already used `^` to
return a value from a method. Here we examine what it does when it appears
*inside a block*, because that is where Smalltalk parts company with every
mainstream language.

## 6.1 `^` in a method body

A `^` written directly in a method body is an ordinary return: it produces a
value and ends the method.

```smalltalk
Account >> balance
  ^ balance.
```

Nothing exotic — `^ balance` answers the instance variable and the method is
over. This is `return balance` in Python or JavaScript.

A method may have several `^`s; the first one reached wins:

```smalltalk
"-- classify.st --"
Object subclass: #Grader instanceVariableNames: ''.

Grader >> classify: score
  (score >= 90) ifTrue: [ ^ 'A' ].
  (score >= 80) ifTrue: [ ^ 'B' ].
  ^ 'C'.

(Grader new) classify: 85.
```

```bash
$ ./build/protost classify.st
B
```

Wait — there is a `^` *inside a block* here: `[ ^ 'A' ]` is the argument to
`ifTrue:`. That is precisely the case this chapter is about.

## 6.2 The home method

Here is the rule, and it is the whole chapter:

> A `^` inside a block returns from the **method that textually contains the
> block** — its *home method* — not merely from the block.

When `Grader>>classify:` runs with `score` = 85, the first `ifTrue:` block is
skipped (`85 >= 90` is false). The second block, `[ ^ 'B' ]`, runs. Its `^`
does **not** just end the block — it ends `classify:` itself, abandoning the
trailing `^ 'C'`. The method's result is `'B'`.

The block where the `^` is written is called the *home method's* block; the
home method is `classify:`. The `^` always targets the home method, however
deeply the block is nested.

```smalltalk
"-- first-even.st --"
Object subclass: #Finder instanceVariableNames: ''.

Finder >> firstEven: aCollection
  aCollection do: [ :x |
    (x \\ 2 = 0) ifTrue: [ ^ x ] ].
  ^ nil.

(Finder new) firstEven: #(1 3 5 8 9).
```

```bash
$ ./build/protost first-even.st
8
```

Trace it. `firstEven:` calls `do:`, which evaluates the block once per element.
For `1`, `3`, `5` the inner `ifTrue:` block is skipped. For `8`, the inner
block `[ ^ x ]` runs — and its `^` returns `8` *from `firstEven:`*. The `do:`
loop is abandoned mid-iteration; `9` is never visited; the trailing `^ nil` is
never reached. The `^` is two block-nestings deep (`do:`'s block, then
`ifTrue:`'s block) and still it returns from the method.

This is what "non-local return" means: the return jumps *out of* the block,
past the loop, past everything, straight to the home method's caller. It is the
Smalltalk idiom for "I found my answer, stop everything, hand it back".

> **In Python** there is no equivalent. A `return` inside a nested function
> returns only from *that* function, never from the enclosing one. To break out
> of a loop early you `break`; to leave a function early you `return` — two
> different mechanisms, and neither crosses a function boundary. **In
> JavaScript** it is the same: `return` inside an arrow function returns from
> the arrow function. **In protoST** there is *one* mechanism, `^`, and it
> always targets the home method, no matter how many blocks it sits inside.
> `aCollection do: [ :x | … ^ x … ]` is the idiomatic early-exit-from-a-loop —
> it is `for x in coll: … return x` collapsed into one construct.

> **Smalltalker note.** This is standard Smalltalk-80 non-local return
> semantics, unchanged. protoST implements it faithfully: `^` from arbitrary
> block-nesting depth returns from the home method activation. There is one
> protoST-specific wrinkle worth knowing — the *dead-home* case, §6.4 — which
> protoST turns into a catchable exception rather than a hard crash.

## 6.3 Falling off the end of a block

Contrast `^` with a block that simply *ends*. A block with no `^` yields its
last statement's value to whoever evaluated it — a *local* block return. It
does not touch the enclosing method.

```smalltalk
"-- fall-off.st --"
Object subclass: #Calc instanceVariableNames: ''.

Calc >> doubled: n
  | result |
  result := [ n * 2 ] value.    "block has no ^ — yields n*2 to `result`"
  ^ result + 1.

(Calc new) doubled: 10.
```

```bash
$ ./build/protost fall-off.st
21
```

The block `[ n * 2 ]` has no `^`. Evaluating it yields `20` *to the `value`
send*, which is stored in `result`. Execution continues to `^ result + 1`,
giving `21`. Compare: had the block been `[ ^ n * 2 ]`, the `^` would have
returned `20` from `doubled:` directly, and `^ result + 1` would never run.

The rule, restated as a pair:

- **`[ … expr ]`** — the block *yields* `expr` to its evaluator. Local. The
  enclosing method continues.
- **`[ … ^ expr ]`** — the block *returns* `expr` from the home method. Drastic.
  The enclosing method ends.

Use a bare last statement when you want a value computed; use `^` when you want
to *leave the method now*.

## 6.4 The dead-home case

A block is an object, and an object can outlive the method that created it —
you can return a block from a method, store it, and evaluate it much later. If
that escaped block contains a `^`, and you evaluate it *after its home method
has already returned*, the `^` has no live method to return from. This is the
**dead-home** condition.

protoST does not crash on it. It signals a catchable `BlockCannotReturn`
exception — a subclass of `Error` — which an ordinary exception handler
([Chapter 7](07-exceptions.md)) can catch:

```smalltalk
"-- dead-home.st --"
Object subclass: #Maker instanceVariableNames: ''.

Maker >> escapedBlock
  ^ [ ^ 42 ].                  "returns a block that itself contains a ^"

blk := (Maker new) escapedBlock.   "escapedBlock has now returned — its home is dead"
result := [ blk value ] on: Error do: [ :e | e messageText ].
result.
```

```bash
$ ./build/protost dead-home.st
non-local return: home method has already returned
```

`escapedBlock` returns the block `[ ^ 42 ]` and *then ends*. Later, evaluating
`blk` runs `[ ^ 42 ]`, whose `^` tries to return from `escapedBlock` — but
`escapedBlock` is long gone. protoST raises `BlockCannotReturn`; the
`on: Error do:` handler catches it and reads its message text.

In everyday code you will rarely hit this — it requires deliberately escaping a
`^`-bearing block. The point is only that protoST treats it as a *catchable
condition*, not as a fatal error: a misbehaving escaped block cannot take the
whole program down.

> **In Python/JS** this situation simply cannot arise — a closure's `return`
> only ever leaves the closure, never an already-finished enclosing function.
> The dead-home case is a peculiarity of Smalltalk's non-local return, and
> protoST handles it as gracefully as it can: a catchable exception with a
> clear message.

## 6.5 Summary

- `^` in a method body is an ordinary return.
- `^` *inside a block* returns from the block's **home method** — the method
  that textually contains the block — abandoning any loops and trailing code,
  from any nesting depth. This is *non-local return*.
- `aCollection do: [ :x | cond ifTrue: [ ^ x ] ]` is the idiomatic
  early-exit-from-a-loop.
- A block with **no** `^` falls off its end and *yields* its last value
  locally; the enclosing method continues.
- Evaluating an escaped `^`-bearing block after its home method has returned —
  the *dead-home* case — signals a catchable `BlockCannotReturn` rather than
  crashing.

---

Next: [Chapter 7 — Exceptions](07-exceptions.md)
