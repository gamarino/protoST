# Chapter 7 — Exceptions

[Tutorial index](../TUTORIAL.md) · Previous: [Chapter 6](06-non-local-return.md) · Next: [Chapter 8 — Collections](08-collections.md)

---

When something goes wrong — a missing dictionary key, an out-of-range index, a
`doesNotUnderstand`, an error your own code signals — protoST raises an
**exception**. This chapter shows how to signal one, how to catch one, and how
the Smalltalk exception protocol gives you options no `try`/`except` block has.

## 7.1 The exception hierarchy

Exceptions are objects, and exception *kinds* are classes. protoST gives you
three to start from:

```
Exception        "the root — resumable"
  Error          "a serious fault — NOT resumable"
  Warning        "a non-fatal condition — resumable"
```

- **`Error`** is the one you will use most: a genuine fault — a bad argument, a
  failed precondition, a `MessageNotUnderstood`. An `Error` is **not
  resumable** (more on that in §7.6).
- **`Warning`** is a non-fatal condition the program can note and carry on past.
- **`Exception`** is the abstract root; you rarely signal it directly.

You make your own exception types by subclassing, exactly as in
[Chapter 5](05-classes-and-methods.md):

```smalltalk
Error subclass: #InsufficientFunds
  instanceVariableNames: ''.
```

A subclass instance *is* an instance of its superclass for the purpose of
catching — an `InsufficientFunds` is caught by a handler for `Error`.

> **In Python** the hierarchy is `BaseException` → `Exception` → your classes,
> caught with `except`. **In JavaScript** it is the `Error` family, caught with
> `catch`. **In protoST** the hierarchy is `Exception`/`Error`/`Warning` and —
> the structural difference — an exception is *not* a privileged language
> construct. `Error` is an ordinary class, `signal` is an ordinary message,
> `on:do:` is an ordinary keyword message. There is no `try` keyword because
> there are no keywords; there is a message that takes blocks.

## 7.2 Signalling an exception

You raise an exception by sending it `signal`, or `signal:` with a message text:

```smalltalk
Error signal.                    "raise a bare Error"
Error signal: 'disk is full'.    "raise an Error carrying a message text"
```

`Error signal: 'disk is full'` builds an `Error` instance, attaches the text,
and raises it. With no handler in scope, an `Error`'s *default action* aborts
the current computation and reports the text:

```bash
$ ./build/protost -e "Error signal: 'disk is full'"
error: disk is full
```

A `Warning` with no handler instead prints and *resumes* — the program carries
on. That difference (abort vs. resume) is what separates a fault from a notice.

You can also signal an existing instance — useful for a custom exception you
want to populate before raising. And recall from
[Chapter 2](02-objects-and-messages.md) that sending an object a message it
does not understand signals a `MessageNotUnderstood`, itself a subclass of
`Error` — so `doesNotUnderstand` flows through this same machinery.

## 7.3 Catching an exception: `on:do:`

A *protected block* is caught with `on:do:` — a keyword message sent to a
block:

```smalltalk
[ protectedCode ] on: ExceptionClass do: [ :ex | handlerCode ]
```

`on:do:` evaluates the receiver block. If it completes normally, `on:do:`
yields its value. If it signals an exception matching `ExceptionClass`, the
handler block runs with the exception instance bound to `:ex`.

```smalltalk
"-- catch.st --"
result := [ Error signal: 'boom' ]
  on: Error
  do: [ :e | 'caught: ' , e messageText ].
result.
```

```bash
$ ./build/protost catch.st
caught: boom
```

The protected block raised an `Error`; the handler caught it, read its
`messageText`, and produced a string — and *that string* became the value of
the whole `on:do:` expression. Note `on:do:` is an expression: it has a value,
you can assign it, exactly like every other message send.

A handler only catches exceptions of its guard class (or a subclass). An
exception of an unrelated class propagates outward, past this handler, to the
next enclosing one. To guard against two unrelated classes in one construct,
use `on:do:on:do:`:

```smalltalk
"-- two-guards.st --"
r := [ Warning signal: 'low ink' ]
  on: Error   do: [ :e | 'an error: ' , e messageText ]
  on: Warning do: [ :e | 'a warning: ' , e messageText ].
r.
```

```bash
$ ./build/protost two-guards.st
a warning: low ink
```

> **In Python** this is `try: … except ExcClass as e: …`. **In JavaScript**,
> `try { … } catch (e) { … }`. **In protoST** it is `[ … ] on: ExcClass do:
> [ :e | … ]` — a message, taking two blocks: the protected code and the
> handler. The exception arrives as the handler block's argument, which is why
> the handler block declares `:e`. `on:do:on:do:` is protoST's multi-`except`
> — and because it is just a longer keyword selector, there is no limit-of-the-
> grammar feeling to it; it is the same construct with one more pair.

## 7.4 Handler actions: more than `catch`

Here is where the Smalltalk protocol does something Python and JavaScript
cannot. A `try`/`except` block has exactly one outcome: the exception is
swallowed and the handler's code runs *instead of* the rest of the protected
block. protoST gives the handler a **choice**, by sending messages to the
exception object `:e`:

| Handler action | What happens |
|----------------|--------------|
| handler falls off its end | `on:do:` yields the handler's last value; the protected block is abandoned. |
| `e return: v` | `on:do:` yields `v`; the protected block is abandoned. |
| `e retry` | the protected block is **re-run from the start**. |
| `e resume: v` | the `signal` call itself *returns* `v`; the protected block **continues** from where it signalled. (Resumable exceptions only.) |
| `e pass` | the handler declines; the search continues *outward* to the next enclosing handler. |

The default — falling off the end of the handler — is the `try`/`except`
behaviour you know. The other four are new tools.

### `return:` — supply a fallback value

```smalltalk
"-- fallback.st --"
r := [ Error signal: 'parse failed'. 'never reached' ]
  on: Error
  do: [ :e | e return: 'default config' ].
r.
```

```bash
$ ./build/protost fallback.st
default config
```

`e return: 'default config'` makes `'default config'` the value of the whole
`on:do:`, abandoning the protected block. (Falling off the handler's end with
`'default config'` as the last statement does the same thing — `return:` is the
explicit form, and it can be used from anywhere inside the handler, not just at
its end.)

### `retry` — try again

`retry` re-runs the *entire* protected block from the start. This is the
canonical pattern for "attempt an operation that may transiently fail":

```smalltalk
"-- retry.st --"
Object subclass: #Flaky instanceVariableNames: 'tries'.

Flaky >> initialize
  tries := 0.
  ^ self.

Flaky >> attempt
  tries := tries + 1.
  tries < 3 ifTrue: [ ^ Error signal: 'not yet' ].
  ^ 'succeeded on try ' , tries printString.

f := Flaky new.
f initialize.
[ f attempt ] on: Error do: [ :e | e retry ].
```

```bash
$ ./build/protost retry.st
succeeded on try 3
```

The first two `attempt`s signal an `Error`; each time the handler runs `retry`,
which re-evaluates `[ f attempt ]`. The third attempt succeeds. There is no
loop in sight — `retry` *is* the loop.

> **In Python/JS** retrying means writing an explicit `while`/`for` around the
> `try`. **In protoST** `retry` is a handler action — the re-run is built into
> the exception protocol.

### `resume:` — continue past the signal

`resume:` is the most un-`try`/`except`-like action. It makes the *`signal`
call itself* return a value, so the protected block continues from exactly
where it raised the exception, as if `signal` had been a normal expression all
along. Only *resumable* exceptions support it (`Warning` and `Exception`, not
`Error` — §7.6):

```smalltalk
"-- resume.st --"
r := [ Warning signal: 'unusual but ok'. 'finished' ]
  on: Warning
  do: [ :e | e resume: nil ].
r.
```

```bash
$ ./build/protost resume.st
finished
```

The protected block signals a `Warning`, the handler runs `e resume: nil`, and
the `Warning signal:` expression *returns* (with `nil`); execution continues to
`'finished'`, which becomes the result. With a plain `try`/`except` the code
after the raise is unreachable; with `resume:` it runs.

### `pass` — decline and propagate

`pass` says "this handler will not deal with the exception after all" — the
search continues outward to the next enclosing handler, as though this handler
were not there. Use it when a handler can deal with *some* cases but wants to
defer the rest.

## 7.5 Cleanup: `ensure:` and `ifCurtailed:`

Some code must run whether or not an exception fired — closing a file, releasing
a resource. Two messages on `Block` register such cleanup:

- **`[ work ] ensure: [ cleanup ]`** — `cleanup` runs **always**: whether
  `work` completes normally or is abandoned by an exception unwinding through.
- **`[ work ] ifCurtailed: [ cleanup ]`** — `cleanup` runs **only** when `work`
  is abandoned abnormally (an unwind passes through); on normal completion it
  does *not* run.

```smalltalk
"-- ensure.st --"
log := OrderedCollection new.
[ [ log add: 'opened'. Error signal: 'fault' ]
    ensure: [ log add: 'closed' ] ]
  on: Error
  do: [ :e | log add: 'handled' ].
log size.
```

```bash
$ ./build/protost ensure.st
3
```

The protected block adds `'opened'`, then signals. The `ensure:` cleanup adds
`'closed'` as the stack unwinds. The outer handler adds `'handled'`. Three
entries — the `ensure:` block ran even though the body raised.

> **In Python** `ensure:` is the `finally` clause; `ifCurtailed:` is `finally`
> minus the normal-completion case (Python has no direct equivalent — you would
> set a flag). **In JavaScript** `ensure:` is `try { } finally { }`. **In
> protoST** both are messages on a block — `ensure:` and `ifCurtailed:` — not
> grammar. The cleanup is a block, the same kind of object you have been using
> all along.

## 7.6 Resumability

Whether an exception can be `resume:`d is a property of its class:

- `Exception` and `Warning` are **resumable** — `resume:` works.
- `Error` is **not resumable** — calling `resume:` on a caught `Error` is
  itself an error.

The reasoning: an `Error` signals that a computation reached a state it cannot
sensibly continue from, so "continue past the signal" is meaningless. A
`Warning` signals something merely unusual, so continuing is reasonable. When
you subclass, the resumability is inherited — a subclass of `Error` is
non-resumable, a subclass of `Warning` is resumable.

## 7.7 A worked example

Putting the pieces together — a custom exception, a handler that supplies a
fallback, and `ensure:` cleanup:

```smalltalk
"-- bank.st --"
Error subclass: #InsufficientFunds
  instanceVariableNames: ''.

Object subclass: #Account
  instanceVariableNames: 'balance'.

Account >> initialize
  balance := 100.
  ^ self.

Account >> balance
  ^ balance.

Account >> withdraw: amount
  ^ (amount > balance)
      ifTrue:  [ InsufficientFunds signal: 'insufficient funds' ]
      ifFalse: [ balance := balance - amount. balance ].

Account >> safeWithdraw: amount
  ^ [ self withdraw: amount ]
      on: InsufficientFunds
      do: [ :e | 'refused: ' , e messageText ].

a := Account new.
a initialize.
a safeWithdraw: 250.
```

```bash
$ ./build/protost bank.st
refused: insufficient funds
```

`safeWithdraw: 250` calls `withdraw:`, which signals the custom
`InsufficientFunds` (250 exceeds the balance of 100). The
`on: InsufficientFunds do:` handler catches it and yields the message text with
a prefix — caught by this handler because `InsufficientFunds` is a subclass of
`Error`'s sibling… in fact a subclass of `Error` itself, so an `on: Error do:`
handler would catch it just as well.

> **A parser caveat worth one paragraph.** Notice `withdraw:` is written as an
> `ifTrue:ifFalse:` *expression* — `^ (cond) ifTrue: […] ifFalse: […]` — rather
> than as a *guard clause* (`(cond) ifTrue: [ ^ … ].` followed by more
> statements). On the current build, a guard clause whose `ifTrue:` block
> contains a `^` returning a bare instance variable, followed by further
> statements that touch that same variable, can be mis-parsed. The robust,
> always-correct shape — and the one this tutorial uses throughout — is the
> *expression* form: compute the whole result with `ifTrue:ifFalse:` and `^` it
> once at the end. It reads well and side-steps the caveat entirely.

## 7.8 Summary

- Exceptions form a class hierarchy: `Exception` → `Error` (non-resumable),
  `Warning` (resumable). Subclass any of them for your own exception types.
- Raise with `anException signal` / `signal: 'text'`. With no handler, an
  `Error` aborts; a `Warning` prints and resumes.
- Catch with `[ protected ] on: ExcClass do: [ :e | handler ]` — a message
  taking two blocks. `on:do:on:do:` guards two classes at once.
- The handler chooses the outcome: fall off its end, `return:` a value, `retry`
  the protected block, `resume:` past the `signal` (resumable exceptions), or
  `pass` the exception outward.
- `[ work ] ensure: [ cleanup ]` always runs the cleanup;
  `[ work ] ifCurtailed: [ cleanup ]` runs it only on an abnormal exit.

---

Next: [Chapter 8 — Collections](08-collections.md)
