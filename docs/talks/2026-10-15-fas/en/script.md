# protoST — talk script (FAST, 15/10/2026)

Length: a 30-minute core; optional modules up to 60. Every performance figure
comes from `benchmarks/reports/2026-10-11-protocore-2.14.5.md` (measured on 11/10/2026 on `main` with protoCore 2.14.5, the code of
release 0.6.0, with the 0.5.0 release on protoCore 2.6.2 run alongside; synthetic benchmarks, on a
laptop); if a figure is not in that report, it is not said.

Before starting: terminal with a large font, `cd docs/talks/2026-10-15-fas/demos`,
`protost --version` must print `protoST 0.6.2`; demo 4 needs `python3`. The fallback recordings are in
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

### 0:02 — Questions, not answers (3 min)

Put them to the audience as questions (do not assert them on their behalf):

- What does it take you today to put an image behind something else as a
  service?
- How many cores does your Smalltalk code use when the machine has twelve?
- How do you integrate what you write in Smalltalk with the rest of the stack
  (Python, JavaScript, queues, databases)?
- How do you share code as plain text with someone who does not use an image?

We do not say that protoST solves all of that. We say that it opens a
different way to try.

### 0:05 — Why it is possible now: protoCore (5 min)

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

### 0:10 — DEMO 1: the code you already know (5 min)

`./run_demo.sh 01-familiar-code.st`

Show the file before running it (classes, `printOn:`, blocks, non-local
return in `Bank>>find:`, a custom exception with `retry`, `ZeroDivide` with
`resume: 0`, exact fractions, `LargeInteger`). Points to highlight:

- it is a text file; a blank line ends a method;
- `new` sends `initialize`, `printOn:` governs how everything is printed;
- the last line prints the total (270): a script shows only what it prints.

Fallback: `scriptreplay` of `recordings/01-familiar-code`.

### 0:15 — Actors and real parallelism (3 min)

- Any object becomes an actor with `asActor`; every message returns a
  `Future` at once (except those that ask about the reference itself:
  identity, equality, `hash`, `isNil`, `isActor`, `printString`, which the
  proxy answers immediately); the actor processes one message at a time, so
  its state is plain instance variables, with no locks.
- Actors share a pool of native threads.
- `wait`, `thenDo:`, `whenAll:`. An error inside the actor comes back, with
  its class, to whoever is waiting.

### 0:18 — DEMO 2: the same computation on one thread and on twelve actors (3 min)

`./run_demo.sh 02-actors-parallelism.st`

Both results agree (4946 primes) and the times measured at that moment are
shown. State the speedup that appears on screen, not one from memory. Point
out: the code of the computation did not change; what changed is who runs it.

Fallback: `recordings/02-actors-parallelism`.

### 0:21 — Worlds it could open up, and DEMO 4 (6 min)

Ideas, not promises:

- **Digital twins**: one actor per physical component; each event is
  processed atomically; the components talk through messages. (If there is
  time, DEMO 3.)
- **Services and connections to other systems**: a process that starts in
  tens of milliseconds, reads files and standard input, speaks HTTP (client
  and server, https too) and TCP/UDP, and runs other programs; no image to
  deploy. The names are Pharo's: `'data.csv' asFileReference contents`,
  `Stdio stdin nextLine`, `Smalltalk arguments`.
- **Living alongside other runtimes**: protoCore 2.6 lets two runtimes
  embedded in one process read and write each other's objects without
  copying them (tested protoScala ↔ protoST). The installed binaries do not
  load each other yet. Say it exactly like that.

DEMO 4: `./run_demo.sh 04-connected-twin.st`

A Python program emits sensor readings, one JSON object per line; the protoST
twin reads them on standard input (it is a Unix filter:
`python3 04-connected-twin.feed | protost 04-connected-twin.st`), the pump is
an actor, its state is served over HTTP while it runs, and every reading is
logged to a file. Show the code: `Stdio stdin linesDo:`, `JSON parse:`,
`HTTPServer on:handler:`, `writeStreamDo:`. Point out: another language
delivers the data, protoST processes it with actors and exposes it over HTTP;
none of it needed an image.

Fallback: `recordings/04-connected-twin`.

### 0:27 — What protoST is not (2 min)

Read the headlines of chapter 14 of the tutorial: no image or browser, a
blank line ends a method, immutable strings, short symbols equal to strings,
thin metaclass, `thisContext` not supported, an actor that is waiting does not
handle other messages (and a cycle of waits is reported as an error). Show
that it is all written down and verified.

### 0:29 — How to take part (1 min)

- Repository, tutorial (chapter 14 is for you), `docs/STATUS.md` with what is
  open.
- What helps most: real programs written the way you would write them, and
  reports of everything that surprises you.
- Questions.

---

## Modules (up to 60 minutes)

### M1 — Inside protoCore (10 min)

64-byte cells, integers inline in the pointer, immutable AVL lists and ropes,
`ProtoThread` and how protoST uses it for the workers. The atomic snapshot of
a mutable object (drawn on the whiteboard).

The collector as it stands in protoCore 2.14.5: concurrent; the sweep
walks several chains at once and can add helper threads. Since 0.6.2
protoST uses protoCore's adaptive heap controller (before, a fixed 640 MB
ceiling): faster on the actor programs and with a third of the memory. The
cost: stop-the-world pauses in protoST are 4 ms with one thread and
25–31 ms with twelve, because each pause scans every thread's whole frame
array; with the full pool, `parallel_speedup` runs 14 % slower than on
0.6.1. Present it as a measurement, not a hidden defect
(`KNOWN_ISSUES.md`, K5 and K6).

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
- Figures from the 11/10 report (`benchmarks/reports/2026-10-11-protocore-2.14.5.md`), synthetic benchmarks on a laptop
  (Ryzen 5 5500U) shared with other work:
  - startup 32 ms (CPython 36 ms);
  - whole process against CPython with the same N: geometric mean 3.5×,
    dominated by startup;
  - the work alone (startup subtracted): from about 5× (integer loop) to
    30× (`fib`) and 80× (exceptions); the geometric mean came out between
    16× and 27× depending on the run, because with small N the subtraction
    is sensitive to noise; give it as a range;
  - twelve actors, 2.5–2.8× with the pool; `saturation_big`, a ceiling of
    about 2× with four workers;
  - against 0.5.0 on protoCore 2.6.2: the same instructions (±1 %), the
    same time within the noise, about 12 MB less memory on several programs,
    and allocation-heavy actor programs on one worker 4–6 % slower in that
    busy session, within the noise on a quiet machine (bisection report of
    11/10). Say it that way, and that 0.6.1 fixes collection in recursions
    that never loop (fib 29 ran out of memory); 0.6.2 enables the adaptive
    heap controller (report of 12/10: actors 6–26 % faster, the full pool
    14 % slower).
- Say where it is slower (recursive method dispatch, exceptions) without
  dressing it up.

### M4 — Roadmap and open questions (6 min)

Runtimes loading each other (the core allows it since protoCore 2.6),
`thisContext`, tooling (a DAP debugger exists), message-send performance,
WebSockets and HTTP/2.
Ask the audience what would be useful to them first.
