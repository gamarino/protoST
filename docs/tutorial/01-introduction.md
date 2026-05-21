# Chapter 1 — Introduction

[Tutorial index](../TUTORIAL.md) · Next: [Chapter 2 — Objects and messages](02-objects-and-messages.md)

---

## 1.1 What protoST is

protoST is an **actor-native Smalltalk runtime**. Three things in that phrase
matter, and they are worth unpacking before you write a line of code.

**Smalltalk.** protoST is a dialect of Smalltalk-80 — the language that
invented the term "object-oriented" in the form most people now mean it. If
you have never seen Smalltalk, the short version is: it is the most consistent
object-oriented language ever designed. Everything — and *everything* really
means everything: the integer `3`, the boolean `true`, a block of code, a
class itself — is an object, and the only thing you ever do to an object is
**send it a message**. There is no second mechanism. No operators that are not
messages, no control-flow keywords that are not messages, no functions that
live outside objects. One idea, applied without exception.

**Runtime.** protoST is built on **protoCore**, a prototype-based object
kernel written in C++. protoCore also hosts protoJS (a JavaScript runtime) and
protoPython (a Python 3.14 runtime). The same 64-byte memory cell, the same
garbage collector, the same immutable structural-sharing collections, the same
GIL-free threading underlie all three. protoST is the demonstration that one
kernel can host a *third* paradigm — message passing — without flattening it
into the others.

**Actor-native.** This is protoST's distinctive contribution and the part that
is *not* standard Smalltalk. Any object can be promoted to an **actor** with a
single message, `asActor`. Once promoted, every message sent to it runs
asynchronously, on a worker thread, and immediately returns a **future** — a
placeholder for the eventual result. A cooperative scheduler runs thousands of
actors on a small pool of OS threads. This is built into the language; it is
not a library you import.

protoST's intended application is **digital twins**: software models of
real-world things — a pump, a vehicle, a sensor, a patient — that hold state
and react to events. A digital twin is naturally a swarm of small, independent,
stateful entities, and that is exactly what an actor is. Chapters 10 and 13
develop this in depth.

## 1.2 Where you are coming from

### If you are a Python or JavaScript developer

You already know objects and methods. The leap to Smalltalk is not learning
new machinery — there is *less* machinery — it is unlearning the parts of
Python/JS that are special cases. In Python, `a + b` is special syntax that
*happens to* call `a.__add__(b)`; `if`/`while`/`for` are keywords baked into
the grammar; a function is a different kind of thing from a method. In
Smalltalk none of that is true: `a + b` is just a message send, `ifTrue:` is
just a message send, a "lambda" (a block) is a perfectly ordinary object you
send `value` to. The grammar is tiny. Once it clicks, most of what you knew as
"language features" turns out to be ordinary library code you could have
written yourself.

This tutorial introduces every new idea against its Python/JS analogue. When
you see a box like this:

> **In Python** you write `obj.method(arg)`. **In protoST** you write
> `obj method: arg`. The dot becomes a space; the parentheses disappear.

that is your bridge. Read it, anchor the new idea to the old one, move on.

### If you are a Smalltalk programmer

You will find protoST familiar from the first line — the syntax is
Smalltalk-80, the object model is prototype-based but presents as classes, the
exception protocol is the one you know. What you should pay attention to is
where protoST *adds* to or *departs from* the dialect:

- **Actors and futures** (Chapter 10) — built into the language.
- **The extended object model** (Chapter 11) — multiple inheritance and mixins
  via `uses:`, and runtime behaviour composition via `addBehavior:`.
- **The module/venv system** (Chapter 9, Chapter 12) — file-based modules,
  `Import from:`, Python-style virtual environments.
- **The intentional deviations** — no image, no metaclass tower, `new` does
  not auto-send `initialize`, no `Transcript`, no `Character` class, integer
  `/` is truncating. [Chapter 14](14-for-the-smalltalk-programmer.md) is a
  precise catalogue.

Chapter 14 is written for you specifically. Read the others lightly; read that
one carefully.

## 1.3 Installing and building

protoST depends on protoCore, which must be built first. From the protoST
checkout:

```bash
cd protoST
cmake -B build -S .
cmake --build build -j
```

This produces the runtime executable `build/protost`. The rest of the tutorial
assumes you run it as `./build/protost` from the project root.

## 1.4 Running protoST code

There are three ways to run protoST, and you will use all three.

### Run a script file

A `.st` file is a protoST program. Run it with:

```bash
./build/protost script.st
```

The program is a sequence of *top-level forms* — class declarations, method
definitions, and statements — executed in order. The runtime prints the value
of the **last top-level statement**. So a file containing just:

```smalltalk
3 + 4.
```

prints `7`.

> **In Python/JS** a script has no "value" — you `print()` explicitly.
> **In protoST** the script's last statement *is* its value, and the CLI
> prints it for you. It is closer to a REPL evaluating a file than to running
> `python script.py`.

### Evaluate one expression

For a quick check, `-e` evaluates a single expression and prints the result:

```bash
$ ./build/protost -e '3 + 4 * 2'
14
```

(Why `14` and not `11`? Because protoST has no operator precedence — see
[Chapter 2](02-objects-and-messages.md). Read on.)

`-e` is for *one expression*. It does **not** accept a `| temps |` declaration,
and multi-statement programs that need local variables belong in a file or the
REPL.

### The interactive REPL

`-i` starts a read-eval-print loop:

```bash
$ ./build/protost -i
protoST 0.1.0-pre — interactive REPL
:help for commands, :quit or Ctrl-D to exit
protoST> 3 + 4
=> 7
protoST> x := 10
=> 10
protoST> x * x
=> 100
```

The session is persistent: a variable, class, or method you define at one
prompt is still there at the next. It auto-detects incomplete input (an
unclosed bracket, an unfinished method) and keeps reading. Meta-commands begin
with `:` — `:help`, `:vars`, `:load`, `:reset`, `:time`, `:quit`. Chapter 12
covers the REPL in full.

> **In Python** this is the `python` REPL or a Jupyter cell. **In JavaScript**
> it is the `node` REPL or the browser console. The protoST REPL works the
> same way and is the fastest way to try the snippets in this tutorial.

## 1.5 A taste of the language

Here is a complete protoST program — a class, three methods, and some
top-level code. Save it as `counter.st` and run it.

```smalltalk
"-- counter.st: a tiny stateful object --"
Object subclass: #Counter instanceVariableNames: 'value'.

Counter >> initialize
  value := 0.

Counter >> increment
  value := value + 1.

Counter >> value
  ^ value.

c := Counter new.
c initialize.
c increment.
c increment.
c value.
```

```bash
$ ./build/protost counter.st
2
```

Even without knowing the details yet, you can read most of it. `Object
subclass: #Counter …` makes a new class. `Counter >> increment …` defines a
method. `c := Counter new` makes an instance. `c increment` sends it a
message. `^ value` returns a result. The `"…"` text is a comment (double
quotes — single quotes are strings). Everything after this point is teaching
you to read and write that fluently.

Notice one thing already, because it trips up newcomers from every other
language: there is **no `if`, no `while`, no `for`** in that program — and
there would not be in a larger one either. protoST has no control-flow
keywords. Conditionals and loops are *messages*, sent to objects, taking
blocks of code as arguments. [Chapter 2](02-objects-and-messages.md) reveals
how, and [Chapter 4](04-blocks.md) makes you fluent with it.

## 1.6 The digital-twin angle

It is worth saying once, up front, why protoST exists, because it shapes the
later chapters.

A digital twin is a software model of a physical thing that mirrors its state
and reacts to its events. Build a thousand of them — a thousand pumps, a fleet
of vehicles — and you have a thousand small state machines running
concurrently, exchanging messages. Doing that today usually means stitching
together a message broker, a pile of microservices, a database, and a
dashboard: many runtimes, many data models, marshalling at every seam.

protoST's bet is that an actor *is* a digital twin: a private bundle of state,
a mailbox of messages, a guarantee that only one message is processed at a
time. A thousand twins are a thousand actors on one worker pool, in one
process, with one object model. [Chapter 10](10-actors-and-futures.md) builds
the actor model and [Chapter 13](13-worked-example.md) builds a real
twin — an industrial pump with concurrently-read sensors — end to end.

You do not need to care about digital twins to learn protoST. But it explains
why the actor model is at the *centre* of the language and not bolted on the
side.

---

Next: [Chapter 2 — Everything is an object, everything is a message](02-objects-and-messages.md)
</content>
