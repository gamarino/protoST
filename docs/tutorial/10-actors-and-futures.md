# Chapter 10 — Actors and futures

[Tutorial index](../TUTORIAL.md) · Previous: [Chapter 9](09-standard-library.md) · Next: [Chapter 11 — The advanced object model](11-advanced-object-model.md)

---

This is the chapter that makes protoST *protoST*. Everything so far — objects,
messages, blocks, classes, exceptions, collections — is recognisable
Smalltalk-80. The **actor model** is protoST's own contribution: a built-in
concurrency model where any object can become a self-scheduling, thread-safe
unit, and message sends to it run in parallel.

If you are a Smalltalk programmer, this is the first chapter that is *not* the
dialect you know. If you come from Python or JavaScript, this is `async`/`await`
and threads — but reorganised around the object, not around the function.

## 10.1 The problem actors solve

Concurrency is hard because of *shared mutable state*. Two threads touching the
same object's fields at the same time produce races, and the usual cure —
locks — is error-prone, deadlock-prone, and clutters every method.

The actor model removes the problem instead of managing it. An **actor** is an
object with three guarantees:

1. It has **private state** that nothing outside it can touch directly.
2. Messages sent to it go into a **mailbox** and are processed **one at a
   time** — never two at once.
3. Different actors run **in parallel**, on a pool of worker threads.

Because only one message runs on a given actor at a time, that actor's state is
never racing against itself — and you never wrote a lock. Because different
actors run in parallel, you still get real concurrency. The single-message
rule *is* the synchronisation.

protoST builds this into the language. It is not a library you import; it is
two messages — `asActor` and `wait` — and a scheduler.

## 10.2 Promoting an object to an actor

Any object becomes an actor by sending it `asActor`:

```smalltalk
sensor := TempSensor new.
sensor initialize.
actor := sensor asActor.
```

`asActor` answers an **actor proxy** wrapping the original object. The wrapped
object is unchanged — `asActor` does not mutate it. From now on you send
messages to the *proxy*, and the proxy is where the actor guarantees live.

> **In Python** the closest thing is wrapping an object so its methods run on a
> dedicated thread or in an `asyncio` task — there is no built-in. **In
> JavaScript** a Web Worker is an isolated execution context, but you cannot
> simply "promote an existing object" into one. **In protoST** `asActor` does
> exactly that: take an ordinary object, get back a concurrency-safe proxy of
> it, in one message. The object did not have to be *written* as an actor.

## 10.3 Sending to an actor returns a `Future`

Here is the behaviour that changes everything. A message sent to an actor proxy
does **not** run immediately and does **not** block the caller. It is placed on
the actor's mailbox, and the send returns — *right away* — a **`Future`**: a
placeholder for the answer that does not exist yet.

```smalltalk
"-- actor-send.st --"
Object subclass: #Calc
  instanceVariableNames: ''.

Calc >> double: n
  ^ n * 2.

calc := (Calc newChild) asActor.
f := calc double: 21.        "f is a Future — NOT 42, not yet"
f wait.                      "block until the actor finishes; answer 42"
```

```bash
$ ./build/protost actor-send.st
42
```

`calc double: 21` returns instantly with a `Future`. The actor's worker runs
`double:` at some point. `f wait` blocks the caller until the future *settles*,
then answers the resolved value, `42`.

> **In JavaScript** this is `async`/`await` exactly: an `async` function call
> returns a `Promise` immediately; `await` waits for it. A protoST `Future` *is*
> a JavaScript `Promise`, and `wait` *is* `await`. **In Python** it is an
> `asyncio` coroutine returning an awaitable. The mental model transfers
> directly — with one difference: in JS/Python *you* decide which functions are
> `async`. In protoST, *every* send to an actor is asynchronous, automatically,
> because the asynchrony belongs to the actor, not to the method.

## 10.4 The `Future` protocol

A `Future` is `pending` until the actor finishes its message, then either
`resolved` with a value or `rejected` with an exception. You interact with it
through:

| Message | Effect |
|---------|--------|
| `wait` | block until settled; answer the value, or re-raise the rejection |
| `thenDo:` | register a block to run with the value once resolved |
| `catch:` | register a block to run with the cause once rejected |
| `resolve:` | settle the future with a value (for a manually-created future) |
| `rejectWith:` | settle the future with a rejection cause |

`wait` is the synchronous workhorse. `thenDo:` is the *callback* form — it
registers code to run later, without blocking:

```smalltalk
"-- then-do.st --"
Object subclass: #Calc
  instanceVariableNames: ''.

Calc >> square: n
  ^ n * n.

calc := (Calc newChild) asActor.
f := calc square: 9.
captured := nil.
f thenDo: [ :v | captured := v ].
f wait.
captured.
```

```bash
$ ./build/protost then-do.st
81
```

`thenDo:` registers the block; when the future resolves with `81`, the block
runs and stores it. (The `wait` here is only to make the script wait for the
actor before reading `captured` — `thenDo:` itself does not block.)

You can also build a `Future` directly with `Future new` and settle it yourself
with `resolve:` — useful when you need a promise that something *other* than an
actor will fulfil:

```bash
$ ./build/protost -e '| f | f := Future new. f resolve: 99. f wait'
```

(That one is multi-statement — run it as a file in practice; the point is
`Future new` gives a first-class, settle-it-yourself promise.)

## 10.5 The payoff: real parallelism

The single most important consequence of "every actor send returns a future
immediately" is that you can **fire many sends and let them run at once**. You
do not wait for each before starting the next — you start them all, *then*
collect the results.

```smalltalk
"-- parallel.st --"
Object subclass: #Sensor
  instanceVariableNames: ''.

Sensor >> read
  Object sleep: 100.        "simulate 100ms of I/O latency"
  ^ 7.

a := (Sensor newChild) asActor.
b := (Sensor newChild) asActor.
c := (Sensor newChild) asActor.

"Fire all three reads — each returns a Future instantly."
fa := a read.
fb := b read.
fc := c read.

"Now collect — the three reads ran concurrently on different workers."
(fa wait) + (fb wait) + (fc wait).
```

```bash
$ time ./build/protost parallel.st
21

real	0m0,117s
```

Three sensor reads, each taking 100ms, complete in ~117ms — *not* 300ms. The
three actors ran their `read` methods in parallel on different worker threads.
The proof: force the runtime to a single worker and the same script takes
~316ms:

```bash
$ time PROTOST_WORKERS=1 ./build/protost parallel.st
21

real	0m0,316s
```

Same program, same result, ~2.7× the wall-clock time — because with one worker
the three reads are forced to run one after another. The speedup is genuine
parallelism across OS threads, and you did not write a single thread, lock, or
`async` keyword to get it.

> **In JavaScript** the parallel pattern is `await Promise.all([a, b, c])` —
> start all the promises, then await them together. **In protoST** the pattern
> is "send to all the actors first (`fa := a read. fb := b read. …`), wait on
> all the futures after". Same shape: *fan out, then join*. The mistake to
> avoid is the same in both languages — `(a read) wait` immediately, then
> `(b read) wait` — which serialises what could be parallel.

## 10.6 `self` inside an actor, and the synchronisation boundary

When a method runs *on behalf of* an actor, `self` is the **wrapped base
object**, not the proxy. So a self-send inside an actor method — `self
helper` — is an ordinary synchronous dispatch: it does *not* re-enqueue on the
mailbox and does *not* take the actor lock. The actor boundary is crossed only
by sending a message to the *proxy*.

This matters for three rules you must honour:

1. **One actor per wrapped object.** The lock-equivalent belongs to the proxy.
   Wrapping the same object in two proxies and driving both re-introduces the
   races `asActor` was meant to remove.
2. **A pre-`asActor` reference bypasses the lock.** If you keep the original
   object reference and send to *it* (not the proxy) after promotion, that send
   runs on your thread, unsynchronised against the actor's worker.
3. **Actors talk only through proxies.** One actor never reaches inside another
   actor's wrapped object — cross-actor communication is exclusively
   message sends to the proxy.

Follow these and the actor model's safety holds. Break them and you are back to
shared-state concurrency.

> The actor proxy is *fully transparent*: it forwards **every** message
> asynchronously, with no exceptions — even `printString`. Sending
> `printString` to a proxy returns a `Future` resolving to the *wrapped
> object's* `printString`. There is deliberately no synchronous way to ask a
> proxy "are you an actor?" — that opacity is the point. To get a proxy's
> printable form, `wait` on the future: `(actor printString) wait`.

## 10.7 Cooperative yield — scaling past the thread count

There is one more piece, and it is what lets protoST run *thousands* of actors
on a handful of threads.

When an actor's running method sends `wait` to a *pending* future — for
instance, an actor that has delegated work to another actor and now needs the
answer — the actor **suspends cooperatively**. Its worker thread is *released*
to run other actors. When the awaited future settles, the suspended actor
resumes (on some worker) and its method continues from the `wait` point with
the resolved value.

The consequence: a `wait` inside an actor does *not* tie up a worker thread.
So you can have ten thousand actors, each waiting on each other, on a pool of
eight threads — the waiting actors cost nothing while they wait. This is how
the digital-twin pattern scales: a fleet of interdependent twins is a fleet of
mostly-waiting actors, and mostly-waiting actors are nearly free.

> **In Python** this is the `asyncio` event loop — an `await` yields control so
> the loop can run other tasks. **In JavaScript** it is the same single-loop
> cooperative scheduling. **In protoST** the cooperative yield happens on a
> `wait` inside actor code, and crucially it is *multi-threaded* underneath:
> protoST gets `asyncio`-style scaling (cheap waiting) *and* real multi-core
> parallelism (the worker pool) at once. Note one boundary: a `wait` from the
> *main* thread — a script's top level, the REPL — instead blocks that OS
> thread. The main thread is a synchronous client of the actor world.

## 10.8 Iterating over actors — `doYielding:`

The fan-out pattern in §10.5 works because it is *manually unrolled*: you
write each send and each `wait` by name. That is fine for two or three
known actors, but suppose you have an `OrderedCollection` of N actors —
sensors, twins, drivers — and you want each to handle a request and then
collect the answers. The natural Smalltalk shape is `do:`:

```smalltalk
"-- BROKEN if `wait` yields cooperatively --"
sensors do: [ :s | results add: (s read) wait ].
```

This **does not work safely inside an actor method**. `do:` is a
primitive that calls the block via a recursive C++ stack frame; when the
inner `wait` yields cooperatively, the iteration state of `do:` is lost
with the C++ stack unwind. The block runs once and the rest of the
sensors are skipped.

The compiler-recognised selector **`doYielding:`** replaces `do:` for
the case you need iteration with cooperative `wait`:

```smalltalk
"-- worked example: a Driver actor that fan-outs to its sensors --"
Object subclass: #Driver instanceVariableNames: 'sensors'.

Driver >> setSensors: ss  sensors := ss.  ^ self.

Driver >> readAll
  | results |
  results := OrderedCollection new.
  sensors doYielding: [ :s | results add: (s read) wait ].
  ^ results.
```

`doYielding:` is the compiler's bytecode-loop desugar of `do:` — same
iteration semantics, but the loop body lives in bytecode (using `at:` +
`value:`) inside the engine's normal dispatch, so the block can yield
cooperatively without losing place in the iteration.

The shape applies on both legs of a fan-out / fan-in:

```smalltalk
Driver >> readAllParallel
  | requests results |
  requests := OrderedCollection new.
  results  := OrderedCollection new.
  "Fan-out — no wait, no yield, regular `do:` would also work here, but
   `doYielding:` is consistent."
  sensors doYielding: [ :s | requests add: (s read) ].
  "Fan-in — `wait` may yield per element. doYielding: keeps the loop alive."
  requests doYielding: [ :r | results add: r wait ].
  ^ results.
```

**Two limits worth knowing**:

1. **`doYielding:` only works on `SequenceableCollection`s** (Array,
   OrderedCollection, Interval, String) — collections that answer
   `at:` and `size`. Set, Dictionary and Bag still use the regular
   polymorphic `do:`; iteration over those is unchanged but cannot
   contain a `wait`.

2. **`1 to: N do: [...]` is the integer iteration primitive**, also
   non-yieldable. If you need a counted loop where the body yields,
   build the index list first:

   ```smalltalk
   indices := OrderedCollection new.
   1 to: rounds do: [ :i | indices add: i ].   "no wait — safe"
   indices doYielding: [ :i | ... do something with wait ... ].
   ```

The benchmark `benchmarks/actors/multi_producer.st` shows the full
multi-driver fan-out / fan-in pattern using these two rules.

> **In JavaScript** the analogue is `for (const s of sensors)
> { results.push(await s.read()) }` — the `for...of` works correctly
> with `await` because JavaScript loops are themselves bytecode in V8,
> not a primitive that calls back into the engine. `doYielding:` is
> the equivalent: the iteration is bytecode, so `wait` cooperates.

## 10.9 Errors in an actor

An exception unhandled inside an actor method does not crash the program. It
propagates to the actor's worker loop, which **rejects that message's future**
with the exception. The actor stays alive and goes on to its next message.

A `wait` on a rejected future re-raises the rejection, so you catch it with an
ordinary handler ([Chapter 7](07-exceptions.md)):

```smalltalk
"-- actor-error.st --"
Object subclass: #Risky
  instanceVariableNames: ''.

Risky >> attempt
  ^ Error signal: 'the actor failed'.

actor := (Risky newChild) asActor.
f := actor attempt.
[ f wait ] on: Error do: [ :e | 'handled: ' , e messageText ].
```

```bash
$ ./build/protost actor-error.st
handled: Future rejected: the actor failed
```

The actor's `attempt` signals an `Error`; that rejects `f`; `f wait` re-raises
it; the `on: Error do:` handler catches it. Note the message text — `wait`
re-raises the rejection wrapped with a `Future rejected:` prefix, so you can
tell a re-raised actor failure from a directly-signalled one. One caveat worth
knowing: a partial mutation an actor method performed *before* it raised is
**not** rolled back — protoST has no transactional default.

## 10.10 Atoms — lock-free shared cells

An actor serialises *logic* over a piece of state: one message at a time. That
is exactly what you want for a stateful entity — a pump, an account. But
sometimes many actors need to update **one shared value** — a global counter, a
registry, a shared world graph — and there is no logic to serialise, just an
update. Routing every update through a single "owner" actor works, but that
actor becomes a bottleneck: every other actor queues behind it.

For that, protoST gives you a third tool, next to immutable values and actors:
the **`Atom`** — a shared mutable cell updated *lock-free*, by an optimistic
compare-and-swap. (If you know Clojure: an actor is its `agent`, an `Atom` is
its `atom`.)

```smalltalk
total := Atom on: 0.        "a shared cell, starting at 0"
total value.                "=> 0"
```

You update it with **`swap:`** — you hand it a block that maps the current
value to the next one:

```smalltalk
total swap: [ :n | n + 1 ].
total value.                "=> 1"
```

The interesting part is what `swap:` does under contention. It reads the
current value, applies your block, and *compare-and-swaps* the result back —
"store this, but only if nobody changed the cell since I read it." If another
actor won the race, the swap **fails, re-reads the now-current value, and runs
your block again**. No update is ever lost, and no lock is ever taken. Your
block may therefore run more than once, so it must be a pure function of its
argument — no side effects.

Here four actors hammer one shared `Atom` in parallel:

```smalltalk
"-- atom-counter.st --"
total := Atom on: 0.

Object subclass: #Meter.
Meter >> bump: anAtom
  1 to: 250 do: [ :i | anAtom swap: [ :n | n + 1 ] ].
  ^ self.

m1 := Meter new asActor.  m2 := Meter new asActor.
m3 := Meter new asActor.  m4 := Meter new asActor.

f1 := m1 bump: total.  f2 := m2 bump: total.
f3 := m3 bump: total.  f4 := m4 bump: total.
f1 wait.  f2 wait.  f3 wait.  f4 wait.

total value.
```

```bash
$ ./build/protost atom-counter.st
1000
```

Exactly `4 × 250` — not a single increment lost, on four cores, with no lock.

If you want to drive the retry yourself — your own validation, your own
back-off — use the raw compare-and-swap, **`value:ifCurrent:`**. It installs
the new value only if the cell still holds the one you expected, and answers
`true`/`false`:

```smalltalk
[ old := total value.
  new := old + 1.
  total value: new ifCurrent: old ] whileFalse.
```

That loop *is* what `swap:` does for you. `whileFalse:` re-runs the block while
the CAS keeps failing; each retry re-reads `total value` — the *updated* value
— and rebuilds from there. This is **optimistic concurrency**: assume no
conflict, do the work, and only retry if the commit is rejected.

The same raw CAS is available on any object's instance variable, without
wrapping it in an `Atom`, as `setInstVar:from:to:`:

```smalltalk
account setInstVar: #balance from: old to: new.   "answers true/false"
```

> **Why this is safe without ABA guards.** A CAS over raw memory has the
> classic *ABA problem*: a value can change A → B → A and a naive CAS cannot
> tell. protoST's CAS compares **immutable snapshots by pointer identity** — if
> the pointer is unchanged, the value genuinely *is* the one you read, because
> it could not have been mutated in place. The hazard simply does not arise.

## 10.11 Summary

- An **actor** is an object with private state, a one-message-at-a-time
  mailbox, and parallel scheduling. The single-message rule is the
  synchronisation — you never write locks.
- `anObject asActor` promotes any object to an actor proxy.
- A message to a proxy returns a **`Future`** *immediately*; the actor runs the
  message later. `wait` blocks for the value (or re-raises a rejection);
  `thenDo:` / `catch:` register callbacks.
- **Fan out, then join**: fire many actor sends (collecting futures), then
  `wait` on the futures — that is what gives real, multi-core parallelism.
- Inside an actor method, `self` is the wrapped object; a self-send is ordinary
  synchronous dispatch. Cross the actor boundary only via the proxy.
- A `wait` *inside* an actor yields cooperatively, freeing its worker — so
  thousands of mostly-waiting actors run on a small thread pool.
- An exception inside an actor rejects that message's future; the actor lives
  on.
- An **`Atom`** is a shared cell updated lock-free: `swap:` runs a pure block
  in a compare-and-swap retry loop; `value:ifCurrent:` is the raw CAS for your
  own retry policy. Use it when many actors update one shared value and an
  owner actor would be a bottleneck.

---

Next: [Chapter 11 — The advanced object model](11-advanced-object-model.md)
