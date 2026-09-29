# protoST — talk script (FAST, 15/10/2026)

Length: a 30-minute core; optional modules up to 60. Every figure comes from
`benchmarks/reports/2026-09-29-release-0.4.0.md`; if a figure is not in that
report, it is not said.

Before starting: terminal with a large font, `cd docs/talks/2026-10-15-fas/demos`,
`protost --version` must print `protoST 0.4.0`. The fallback recordings are in
`../recordings/` (see the checklist).

---

## Core (30 minutes)

### 0:00 — Who and why (2 min)

- This is an experiment looking for enthusiasts, not a product looking for
  users.
- protoST is not a better Smalltalk and does not compete with the live
  environment. It is something else: being able to read and write the
  Smalltalk you know in places Smalltalk does not reach today.
- It is built by one person using AI agents as tools; everything I show is in
  the repository and can be verified.

### 0:02 — Questions, not answers (4 min)

Put them to the audience as questions (do not assert them on their behalf):

- What does it take you today to put an image behind something else as a
  service?
- How many cores does your Smalltalk code use when the machine has twelve?
- How do you integrate what you write in Smalltalk with the rest of the stack
  (Python, JavaScript, queues, databases)?
- How do you share code as plain text with someone who does not use an image?

We do not say that protoST solves all of that. We say that it opens a
different way to try.

### 0:06 — Why it is possible now: protoCore (5 min)

- protoCore: an object core in C++ on which several languages run
  (JavaScript, Python, Clojure, Scala, protoST).
- Three properties, each with a simple example:
  - immutable structures with structural sharing: a "modified" list is a new
    version that shares almost everything with the previous one;
  - native threads with no global lock, and a concurrent GC;
  - a mutable object is an atomic reference to an immutable snapshot: every
    read sees a consistent snapshot.
- From Smalltalk, objects still change: `at:put:` on an `Array` or `add:` on
  an `OrderedCollection` modify the object by installing a new snapshot.
  Strings do not: a String is a protoCore `ProtoString` (a rope, a value with
  no in-place modification).
- Practical consequence: passing an object to another thread means passing a
  reference, without copying it. The object is shared: what one side changes,
  the other sees on its next read.

### 0:11 — DEMO 1: the code you already know (5 min)

`./run_demo.sh 01-familiar-code.st`

Show the file before running it (classes, `printOn:`, blocks, non-local
return in `Bank>>find:`, a custom exception with `retry`, `ZeroDivide` with
`resume: 0`, exact fractions, `LargeInteger`). Points to highlight:

- it is a text file; a blank line ends a method;
- `new` sends `initialize`, `printOn:` governs how everything is printed;
- the value of the last expression (270) is what the script "prints".

Fallback: `scriptreplay` of `recordings/01-familiar-code`.

### 0:16 — Actors and real parallelism (3 min)

- Any object becomes an actor with `asActor`; every message returns a
  `Future` at once (except those that ask about the reference itself:
  identity, equality, `hash`, `isNil`, `isActor`, `printString`, which the
  proxy answers immediately); the actor processes one message at a time, so
  its state is plain instance variables, with no locks.
- Actors share a pool of native threads.
- `wait`, `thenDo:`, `whenAll:`. An error inside the actor comes back, with
  its class, to whoever is waiting.

### 0:19 — DEMO 2: the same computation on one thread and on twelve actors (3 min)

`./run_demo.sh 02-actors-parallelism.st`

Both results agree (4946 primes) and the times measured at that moment are
shown. State the speedup that appears on screen, not one from memory. Point
out: the code of the computation did not change; what changed is who runs it.

Fallback: `recordings/02-actors-parallelism`.

### 0:22 — Worlds it could open up (4 min)

Ideas, not promises:

- **Digital twins**: one actor per physical component; each event is
  processed atomically; the components talk through messages. (If there is
  time, DEMO 3.)
- **Services**: a process that starts in tens of milliseconds and reads
  files, with no image to deploy.
- **Living alongside other runtimes**: this is the direction of the project.
  Today a protoScala program imports a protoST module and receives the same
  object, without a copy; it cannot use it yet. Say it exactly like that.

### 0:26 — What protoST is not (2 min)

Read the headlines of chapter 14 of the tutorial: no image or browser, a
blank line ends a method, immutable strings, short symbols equal to strings,
thin metaclass, `thisContext` not supported, an actor that is waiting does not
handle other messages (and a cycle of waits is reported as an error). Show
that it is all written down and verified.

### 0:28 — How to take part (2 min)

- Repository, tutorial (chapter 14 is for you), `docs/STATUS.md` with what is
  open.
- What helps most: real programs written the way you would write them, and
  reports of everything that surprises you.
- Questions.

---

## Modules (up to 60 minutes)

### M1 — Inside protoCore (10 min)

64-byte cells, integers inline in the pointer, immutable AVL lists and ropes,
the concurrent GC that defers collection until it is needed, `ProtoThread`
and how protoST uses it for the workers. The atomic snapshot of a mutable
object (drawn on the whiteboard).

### M2 — The digital twin step by step (8 min)

`./run_demo.sh 03-digital-twin.st`: the pump as a state machine in an actor,
three sensors with 50 ms of simulated I/O read in parallel on every cycle
(the time per cycle on screen is around 50 ms, not 150), the alarm that stops
the pump when it goes above 75 degrees. Walk through the code.

Fallback: `recordings/03-digital-twin`.

### M3 — Performance and method (6 min)

- Every benchmark verifies its result; the harness fails if the result is
  missing or does not match (a bug in another project that looked like an
  "improvement" because it was not verified was shown).
- Figures from the 0.4.0 report (`benchmarks/reports/2026-09-29-release-0.4.0.md`):
  - startup 27 ms (CPython 29 ms);
  - whole process against CPython with the same N: geometric mean 3.45×,
    dominated by startup;
  - the work alone (startup subtracted): from about 6× (integer loop) to
    36× (`fib`) and 64× (exceptions), geometric mean 20.8×;
  - `saturation_big` scaling: a ceiling of about 2.1× with four workers,
    confirmed by a second run under lighter load; twelve actors, 2.1× with
    the pool.
- Say where it is slower (recursive method dispatch, exceptions) without
  dressing it up.

### M4 — Roadmap and open questions (6 min)

Real interoperability between runtimes (requires work in protoCore),
`thisContext`, tooling (a DAP debugger exists), message-send performance.
Ask the audience what would be useful to them first.
