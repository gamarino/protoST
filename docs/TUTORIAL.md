# The protoST Tutorial

> **A dual-audience tutorial.** This document teaches protoST to two audiences
> at once. If you come from **Python or JavaScript**, it teaches Smalltalk from
> the ground up, with a constant bridge to the concepts you already know. If
> you are a **traditional Smalltalk programmer**, it shows you exactly where
> protoST departs from Smalltalk-80 — the actor model, futures, cooperative
> yield, the extended object model, and the module/venv system.
>
> Track 8 of [`docs/ROADMAP.md`](ROADMAP.md). It is built on
> [`docs/LANGUAGE.md`](LANGUAGE.md) (the authoritative language reference) and
> [`docs/STATUS.md`](STATUS.md) (the live tracker of what works and what
> deviates). The examples that state a result are run against the `protost`
> binary by `tests/docs/run_doc_snippets.py`, so the results shown are the
> ones the current build prints.

The tutorial is split into navigable chapters under
[`docs/tutorial/`](tutorial/). Read it in order if protoST is new to you; jump
to a chapter if you know what you are looking for.

## How to use this tutorial

**If you are a Python or JavaScript developer.** Read every chapter in order,
from [Chapter 1](tutorial/01-introduction.md) to
[Chapter 13](tutorial/13-worked-example.md). Each chapter introduces new ideas
against a Python/JS analogue you already know — message sends versus method
calls, blocks versus lambdas, `ifTrue:` versus `if`. By the end you will be
productive. You can skip [Chapter 14](tutorial/14-for-the-smalltalk-programmer.md),
which is written for the other audience.

**If you are a Smalltalk programmer.** You can skim Chapters 2–9: they are
standard Smalltalk-80 and you will recognise almost everything (the Python/JS
bridges are not aimed at you, and skipping them costs you nothing). Then read
[Chapter 10](tutorial/10-actors-and-futures.md) (actors and futures),
[Chapter 11](tutorial/11-advanced-object-model.md) (multiple inheritance,
mixins, runtime composition) and especially
[Chapter 14](tutorial/14-for-the-smalltalk-programmer.md), which is a precise,
honest catalogue of every way protoST is *not* the dialect you know.

## Chapters

| # | Chapter | What it covers |
|---|---------|----------------|
| 1 | [Introduction](tutorial/01-introduction.md) | What protoST is; how to run it; the digital-twin angle. |
| 2 | [Everything is an object, everything is a message](tutorial/02-objects-and-messages.md) | The core idea; unary/binary/keyword messages and precedence; the "no control-flow keywords" revelation. |
| 3 | [Variables, assignment, literals](tutorial/03-variables-and-literals.md) | `:=`, numbers (including `LargeInteger` and `Float`), strings, symbols, characters, arrays. |
| 4 | [Blocks](tutorial/04-blocks.md) | `[ :x \| … ]`, `value`/`value:`, blocks as closures, control flow as messages. |
| 5 | [Classes, objects, methods](tutorial/05-classes-and-methods.md) | `subclass:`, `>>` methods, instance variables, `self`, `super`, class-side methods. |
| 6 | [Non-local return](tutorial/06-non-local-return.md) | `^` from inside a block; the home method. |
| 7 | [Exceptions](tutorial/07-exceptions.md) | `Error`/`Warning`, `signal`, `on:do:`, `ensure:`, `resume:`/`retry`/`pass`. |
| 8 | [Collections](tutorial/08-collections.md) | `Array`, `OrderedCollection`, `Set`, `Dictionary`, `Bag`, `Interval`; the iteration protocol. |
| 9 | [The standard library](tutorial/09-standard-library.md) | `Stream`, the math protocol, `Random`, `JSON`, `Time`; `Import from:`. |
| 10 | [Actors and futures](tutorial/10-actors-and-futures.md) | `asActor`, asynchronous sends, `Future`/`wait`, cooperative yield. |
| 11 | [The advanced object model](tutorial/11-advanced-object-model.md) | `uses:` multiple inheritance and mixins; `addBehavior:` runtime composition. |
| 12 | [Tooling](tutorial/12-tooling.md) | The REPL and its meta-commands; the DAP debugger; the venv. |
| 13 | [A worked example](tutorial/13-worked-example.md) | A digital-twin program built end to end. |
| 14 | [For the Smalltalk programmer](tutorial/14-for-the-smalltalk-programmer.md) | Every deviation from Smalltalk-80, honestly catalogued. |

## A note on running the examples

Two ways to run protoST code appear throughout:

```bash
./build/protost -e '3 + 4'        # evaluate one expression, print the result
./build/protost script.st         # run a file; it shows only what it prints
```

There is one practical rule worth knowing before you start, because a
Smalltalk browser or workspace never shows it to you: **in a file, a blank
line ends a method body.** A method is `ClassName >> selector` followed by its
statements; the method ends at the next blank line, so write methods without
blank lines inside them and leave a blank line between the last method and
the top-level code that follows it:

```smalltalk
"-- a method, a blank line, then top-level code --"
Object subclass: #Greeter.
Greeter >> greet: name
  ^ 'Hello, ' , name.

Greeter new greet: 'Ada'.        "=> 'Hello, Ada'"
```

At the top level you may declare temporaries with `| … |`, as in a workspace,
or simply assign to a name:

```smalltalk
| d |
d := Dictionary new.
d at: #one put: 1.
d at: #one.        "=> 1"
```
