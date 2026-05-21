# protoST Roadmap

> **This is a seed.** protoST today is a working foundation, not a finished
> language. This document lays out where it goes next and is meant to be
> **driven by the community** — every track below is something a contributor
> can pick up. Treat it as a living document: propose changes via pull request.

## What protoST is (and is not)

protoST is an **actor-native Smalltalk runtime built on protoCore**. Its
purpose is twofold:

1. **A demonstrator of protoCore's potential.** protoST exists to show what
   protoCore can sustain — a prototype-based object model, GIL-free
   concurrency, a custom GC, structural-sharing collections — under a third,
   very different language paradigm (after protoJS and protoPython).
2. **The base for digital twins.** protoST is *not itself* a digital-twin
   platform. It is the substrate: actors as independently-scheduled state
   machines, cooperative yield, a module system. A real twin = protoST +
   physical-interface modules (loaded through the venv, reading and actuating
   the real world) + the user's actor logic.

**Standard-Smalltalk conformance is desirable but not the primary goal.**
protoST aims to be "as close and as compliant as reasonable", but it may add
**non-standard features** where they exhibit something protoCore makes
possible. The measure of success is not "does it match Smalltalk-80" but
"is it a coherent, complete, well-tested language that shows off the core".

## Where protoST stands today

Phases F1–F8 are complete, plus a backlog hardening pass:

| Area | Status |
|------|--------|
| Lexer, parser, bytecode VM (non-recursive engine) | ✅ |
| Closures with capture | ✅ |
| Classes, instances, methods, instance variables | ✅ |
| `self` / `super` sends, class-side methods | ✅ |
| Modules (file-based) + protoCore UMD integration | ✅ |
| Actors, Futures, parallel scheduler, **cooperative yield** | ✅ |
| Interactive REPL | ✅ |
| DAP debug adapter (VS Code) | ✅ |
| Wide bytecode operands (no 256-local ceiling) | ✅ |
| `printString` | ✅ |

What is **missing** is what this roadmap is about: a complete language core,
a real collection hierarchy, a standard library, a defensible test suite, and
the onboarding material to make protoST approachable.

## How this roadmap works

The project uses a **spec → plan → implementation** flow (see
`docs/superpowers/`): a track is first specified, then broken into a
bite-sized plan, then implemented task-by-task with review. A contributor
picking up a track should:

1. Open an issue describing the slice they want to take.
2. Write (or extend) the spec for it under `docs/superpowers/specs/`.
3. Turn it into a plan under `docs/superpowers/plans/`.
4. Implement against the plan, with tests, keeping the suite green.

**Guiding principle — minimal decoration over protoCore.** Before building a
mechanism in protoST, check whether protoCore already provides it (collections,
GC roots, threading, contexts, the UMD module system). Reimplementing what the
core already does is a regression, not a feature.

---

## Tracks

Each track is independent enough to be owned by a different contributor or
group. Dependencies are noted; respect them.

### Track 1 — Language core completeness

**Goal:** make protoST expressive enough to write real programs.

- **Non-local return.** `^expr` inside a nested block must return from the
  *enclosing method*, not just the block. The engine is already non-recursive
  with an explicit `frames_` stack (F6 v3 A), so non-local return is frame-stack
  unwinding — tractable. *Prerequisite for exceptions.*
- **Exceptions.** The Smalltalk exception protocol: `[ ... ] on: E do: [:e| ]`,
  `signal`, `signal:`, `ensure:`, `ifCurtailed:`, and resumable/retry semantics.
  Built on the same unwinding machinery as non-local return.

**Why it matters:** without these, every "demo" stays toy-sized. This is the
single highest-priority track — it unblocks almost everything else.

**Dependencies:** none (non-local return first, then exceptions).
**Size:** large. Good first slice: non-local return alone.

### Track 2 — The collection hierarchy

**Goal:** a real Smalltalk collection protocol, built on protoCore primitives.

protoCore already provides `ProtoList`, `ProtoTuple`, `ProtoString`,
`ProtoSparseList`, `ProtoSet`, `ProtoMultiset` — these cover, or can be
composed into, every Smalltalk collection. The work is the **language-level
hierarchy and protocol**, not new data structures:

- `Collection` → `SequenceableCollection` (`Array`, `OrderedCollection`,
  `Interval`) and `HashedCollection` (`Set`, `Bag`, `Dictionary`).
- The iteration protocol: `do:`, `collect:`, `select:`, `reject:`, `detect:`,
  `detect:ifNone:`, `inject:into:`, `with:do:`, `do:separatedBy:`, etc.
- Map each Smalltalk class onto the right protoCore primitive (`Dictionary`
  ← `ProtoSparseList`, `Bag` ← `ProtoMultiset`, …).

**Why it matters:** the iteration protocol *is* the Smalltalk programming
experience. It also makes the standard library possible.

**Dependencies:** blocks (done); benefits from exceptions (`detect:` signals).
**Size:** large; naturally splits per collection class.

### Track 3 — Advanced object model (the protoCore showcase)

**Goal:** expose object-model capabilities that go *beyond* standard Smalltalk
— this is where protoST most directly demonstrates protoCore.

- **Extensible classes from modules.** Import a class from a module, subclass
  it locally with a new or overridden method, and use `super` to reuse the
  original. `super` already works; this track wires the module + subclass +
  super story end-to-end and documents it as a first-class pattern.
- **Multiple inheritance & class aggregates.** protoCore supports multiple
  parents (`addParent`, `setParents`, `getParents`). Standard Smalltalk is
  single-inheritance; protoST can deliberately offer more — surface multiple
  inheritance and **on-the-fly behavior aggregation** at the language level.
- **Rich mixins.** A syntax/protocol for composing behavior:
  `Object subclass: #Foo uses: {MixinA. MixinB}`, adding behavior to a class
  at runtime, etc.

**Why it matters:** this is the most direct "look what the core enables"
track — capabilities JS and Python classes do not have in the same form.

**Dependencies:** a stable language core (Track 1); modules (done); `super`
(done).
**Size:** medium-large.

### Track 4 — Standard library ("batteries included")

**Goal:** a Python-style standard library — the modules every program needs.

A set of `.st` modules loadable through the module system: streams, date/time,
math, random, structured I/O, JSON, and whatever the collection track does not
already cover. Each module ships with its own tests and docs.

**Why it matters:** "batteries included" is what makes a language usable
without a fight. It is also the **most community-friendly track** — each
module is an independent, self-contained contribution.

**Dependencies:** collections (Track 2), exceptions (Track 1), modules (done).
**Size:** open-ended; one module = one contribution.

### Track 5 — Cross-language UMD interop

**Goal:** import objects/classes defined in protoJS or protoPython into
protoST (and vice-versa) when they share a `ProtoSpace`.

F5 v2 already registers protoST as a UMD provider in protoCore's
`ProviderRegistry`. The next step is a coherent strategy for **using objects
created by another language's runtime** — a Python object imported into
protoST, its methods invoked as Smalltalk messages; a Smalltalk object handed
to protoJS. This is the showcase of the "three runtimes, one core" ecosystem.

**Why it matters:** it makes the ecosystem real rather than three parallel
silos.

**Dependencies:** F5 v2 (done); coordination with protoJS / protoPython.
**Size:** medium; needs cross-project design.

### Track 6 — A complete, defensible test suite

**Goal:** a test suite that proves protoST behaves correctly — not one that
merely confirms "what was implemented is what was implemented".

Today's tests were largely written alongside the code they test. A defensible
suite must be designed **independently of the implementation**:

- Write a **protoST language specification** — the expected behavior of the
  language (core, collections, actors, modules), standard *and* non-standard.
- Build a conformance suite against that spec, authored without reference to
  the implementation details.
- Cover the actor/concurrency model, GC-rooting under pressure, and the
  non-standard extensions explicitly.

**Why it matters:** "188 tests pass" currently measures regression, not
correctness. This track is what lets the project make honest claims.

**Dependencies:** runs alongside everything; the spec should start now and
grow with each track.
**Size:** large, ongoing.

### Track 7 — Onboarding: examples & tutorials

**Goal:** make protoST approachable for developers coming from JavaScript or
Python.

- "Smalltalk for JavaScript developers" and "… for Python developers" guides.
- A progressive tutorial teaching basic Smalltalk (messages, blocks, classes).
- A cookbook of worked examples, including a real digital-twin example once the
  physical-module story (below) exists.

**Why it matters:** a community-driven project needs a low barrier to entry.

**Dependencies:** grows with the language; the basics can start immediately.
**Size:** ongoing.

Tracks 8 and 9 develop this onboarding goal in depth — the tutorial and the
example set are its concrete deliverables.

### Track 8 — A dual-audience tutorial — ✅ done

**Status:** complete. The tutorial is [`docs/TUTORIAL.md`](TUTORIAL.md) plus 14
chapters under `docs/tutorial/`. It teaches protoST from the ground up for
Python / JavaScript developers and catalogues every departure from
Smalltalk-80 for Smalltalk programmers (Chapter 14). Every non-trivial snippet
was verified against the `protost` build.

**Goal:** one extensive, complete tutorial that teaches protoST to two
audiences at once.

- **For developers coming from Python or JavaScript** — teach Smalltalk from
  the ground up, with a constant mental bridge to concepts they already know:
  objects and message sends vs. methods/function calls; blocks vs.
  lambdas/closures/arrow functions; "there are no control-flow keywords —
  `ifTrue:`/`whileTrue:` are messages"; keyword messages vs. named arguments;
  prototypes and classes; the absence of a statement/expression split; etc.
  Every new idea is introduced against its Python/JS analogue.
- **For traditional Smalltalk programmers** — show clearly where protoST
  *departs* from standard Smalltalk: the actor model and futures, cooperative
  yield, the non-standard object-model features (multiple inheritance, class
  aggregates, mixins — Track 3), the module/venv system, and every documented
  deviation in `docs/LANGUAGE.md` §"Known deviations". A Smalltalker should be
  able to read one section and know exactly what is and is not the dialect
  they know.

As extensive and complete as possible — progressive, from first message send
through classes, blocks, exceptions, collections, actors, and digital-twin
patterns. Built on `docs/LANGUAGE.md` (the language reference, Track 6).

**Why it matters:** protoST's natural audiences are exactly JS/Python
developers and Smalltalkers; a tutorial that speaks to both directly is the
single biggest lever on adoption.

**Dependencies:** the language reference (Track 6, slice 1, done); grows as
Tracks 3–5 land.
**Size:** large.

### Track 9 — A comprehensive example set — ✅ done

**Status:** complete. 40 complete, idiomatic, runnable protoST programs live
under `examples/`, grouped by theme (`basics/`, `blocks/`, `collections/`,
`exceptions/`, `nonlocal/`, `actors/`, `stdlib/`, `modules/`, `programs/`) and
indexed by `examples/README.md`. They span focused single-feature
illustrations through genuine end-to-end programs — a recursive-descent
calculator, an RPN interpreter, a Monte-Carlo pi estimate, a JSON data
transform and two digital-twin simulations. Every example carries an
`"EXPECT: …"` directive and is registered as a CTest smoke case via
`run_conformance.sh` (`ctest -R '^examples/'`, 40/40 green).

**Goal:** an extensive set of complete, idiomatic, runnable protoST programs,
covering every feature: the object model, blocks and closures, collections,
exceptions, actors and futures, modules, and digital-twin patterns.

- Examples may live embedded in the Track 8 tutorial *and* as standalone files
  under `examples/` — ideally both: the tutorial references runnable examples.
- Every example is executable and verified; the example set doubles as a
  smoke layer for the Track 6 conformance suite.
- Range from one-liners illustrating a single selector to full small programs
  (a digital-twin simulation, a worked actor pipeline, a parser, …).

**Why it matters:** runnable examples are the most trustworthy documentation
and the fastest way for a newcomer to get productive.

**Dependencies:** the language; grows with every feature.
**Size:** open-ended — one example is one contribution.

### Track 10 — Installation packaging — ✅ done

**Status:** complete. `protoST/CMakeLists.txt` carries `install()` rules and a
CPack configuration; `cpack -G DEB` and `cpack -G TGZ` are verified on Linux.

**Goal:** native installers so protoST can be installed without a source
build — a Debian/Ubuntu `.deb`, an RPM and a `.tar.gz` on Linux, a drag-and-drop
`.dmg` on macOS, and an NSIS installer (plus a plain `.zip`) on Windows, all
driven by CPack — and a packaged binary that resolves the standard library out
of the box.

- `install(TARGETS protost …)` places the binary under `<prefix>/bin`; the
  stdlib `.st` modules ship under `<prefix>/share/protoST/lib`, and the T4-a
  discovery in `src/runtime/STRuntime.cpp` probes an install-relative
  `<exe>/../share/protoST/lib`, so an installed `bin/protost` resolves
  `Import from: 'stream'` with no `PROTOST_LIB` set. `LICENSE` and the `docs/`
  tree are installed too.
- The Debian package declares a dependency on protoCore
  (`CPACK_DEBIAN_PACKAGE_DEPENDS = protocore`) and the RPM a matching
  `Requires: protoCore` — protoST links `libprotoCore`, so the package metadata
  must declare it. Install RPATH (`$ORIGIN/../lib`,
  `@executable_path/../lib`) lets the installed binary find `libprotoCore`
  without `LD_LIBRARY_PATH` / `DYLD_LIBRARY_PATH`.

**Why it matters:** a one-command install (`apt`/`dpkg`, the `.dmg`, the
Windows installer) removes the build-from-source barrier and is a prerequisite
for protoST being *usable* rather than only *buildable*.

**Dependencies:** a working build (Tracks 1–4) and protoCore's own packaging.
**Size:** small.

---

## Beyond the language: the digital-twin layer

Once the language core is solid, the distinguishing work begins: the
**physical-interface modules**. These are modules — added to the system and
loaded through the venv — that read and actuate the real world (sensors,
actuators, fieldbus protocols, MQTT, …). protoST core stays paradigm-pure;
the twin is built *on top* by composing these modules with actor logic.
A persistence / actor-snapshot story belongs here too. This is deliberately
out of the language-core roadmap — it is the application layer the core
enables.

## Suggested ordering

```
        Track 1 (core: non-local return → exceptions)   ── highest priority
              │
              ├──────────────► Track 2 (collections)  ──► Track 4 (stdlib)
              │
              └──────────────► Track 3 (object model showcase)

  Track 5 (cross-language interop)   ── parallel, needs cross-project design
  Track 6 (defensible test suite)    ── starts now, runs continuously
  Track 7 (onboarding)               ── starts now, grows continuously
  Track 8 (dual-audience tutorial)   ── after the language reference (Track 6)
  Track 9 (comprehensive examples)   ── grows with every feature
  Track 10 (installation packaging)  ── needs a working build (Tracks 1–4)
```

A reasonable **1.0 milestone**: language core complete (Track 1), the
collection hierarchy (Track 2), an essential standard library (a first slice
of Track 4), a defensible test suite (Track 6), and onboarding — the tutorial
and example set (Tracks 7–9). The showcase tracks (3, 5) and the digital-twin
layer can land before or after 1.0 — they are what make protoST *interesting*,
but 1.0 is what makes it *usable*.

## Contributing

protoST is at an ideal stage for contributors interested in language runtimes,
compilers, concurrency, and object models. Pick a track, open an issue, follow
the spec → plan → implementation flow, and keep the suite green. Small,
self-contained slices (one collection class, one stdlib module, one tutorial)
are the easiest way in.

This roadmap is a seed. Improve it.
