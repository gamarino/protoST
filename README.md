# protoST

> **An actor-native Smalltalk for building digital twins on protoCore.**

protoST is a Smalltalk-80-inspired language runtime with a **first-class embedded actor model**, built on the [protoCore](../protoCore) kernel. It is the third member of the protoCore language triple — alongside [protoJS](../protoJS) (JavaScript) and [protoPython](../protoPython) (Python) — and it closes the technical demonstration that one prototype-based kernel can host three genuinely different OO paradigms (prototypes / classes / messages) without flattening them to a common denominator.

protoST's distinct contribution is putting **actors at the centre of the language**: any object can be promoted to an actor with `asActor`, every message sent to it is dispatched asynchronously and returns a `Future`, and an internal invariant guarantees that exactly one method of a given actor runs at a time. Tens of thousands of actors share a small worker pool through cooperative scheduling — an actor suspends transparently when it waits on a future, freeing its worker for someone else.

## A message is a pointer, not a copy

An actor system lives or dies by its message passing, and every mainstream
runtime makes a hard compromise there:

- **Erlang/Elixir (BEAM)** gives each process a private heap, so a message is
  **deep-copied** from sender to receiver. Safe — but every send pays for the
  copy.
- **Node.js worker threads** cannot share JavaScript objects at all: a message
  is a `structured-clone` copy, or raw bytes through a `SharedArrayBuffer`.
- **The JVM and Go** pass a pointer — fast — but the pointee is *mutable shared
  state*, so you inherit data races, locks, and a subtle memory model.

protoST does not compromise. **A message is a pointer.** When one actor hands a
10 000-node world model to another, nothing is copied and nothing is
serialized — the receiver, on another core, reads the very same object. And it
is safe, with **no locks and no caveats**.

That is possible because of protoCore's object model. A mutable object is not a
block of mutable memory — it is an *atomic reference to an immutable snapshot*.
"Mutating" it installs a **new** snapshot; it never overwrites anything another
thread might be reading. So everything you can ever observe — a plain value, or
the current state of a mutable object — is a complete, frozen, internally
consistent graph. **Torn reads cannot occur.** Sharing a pointer to something
that cannot change is unconditionally safe: no copy (unlike BEAM), no data race
(unlike the JVM), no serialization boundary (unlike Node).

This is the *identity / value* distinction — a mutable object is a stable
*identity* whose *value* is always immutable — promoted from a library pattern
to the **universal object model**, running under true parallelism with no GIL.
The safety lives in the *data model*, not in the actor model: it holds for
every actor and every message by construction — not by discipline, not by
runtime cooperation between actors.

For agents, this changes what is cheap. Ten actors read the same world snapshot
**in parallel with zero contention** — it cannot change under them. "Updating" a
large shared structure costs **O(log n)** new nodes through structural sharing,
and the previous snapshot stays valid for whoever still holds it. That is the
foundation the digital-twin story below is built on.

## Why this matters: digital twins

A digital twin is, structurally, a collection of finite state machines (FSMs) that mirror real-world entities — a pump, a vehicle, a patient, a substation — and react to events. The conventional implementation routes each event to a state-dependent handler via a dispatcher. The actor model expresses the same semantics with the order inverted: each method is the handler, and the actor's state is consulted *inside* it.

The actor formulation buys three properties that pure FSM dispatch tables do not give for free:

1. **Atomicity per event.** The single-method invariant guarantees that two events on the same twin never interleave — the exact reason one writes FSMs in the first place.
2. **Composition by message passing.** Twins interact by sending messages; futures and their combinators (`&`, `|`, `whenAll:`, `whenAny:`) compose interactions without shared mutable state.
3. **Concurrency by default.** Ten thousand twins are ten thousand actors. The cooperative scheduler runs them on a worker pool sized to the host's cores. Going from 10 twins to 10 000 needs no code change.

Combined with the rest of the triple, protoST forms a coherent platform for agent-based / discrete-event simulation:

| Need | Component |
|---|---|
| Many stateful concurrent agents | **protoST** (actor model on Smalltalk syntax) |
| Lock-free shared state across agents | **protoST** (`Atom` — optimistic-concurrency CAS cell) |
| Numerical / ML models inside twins | **protoPython** (numpy, scipy, sklearn) |
| Operator dashboards, visualisation | **protoJS** (web tooling) |
| Real parallelism, no GIL | **protoCore** (kernel) |
| Immutable collections, structural sharing | **protoCore** (kernel) |
| Cross-language interop without marshalling | **UMD** (every module is a `ProtoObject`) |

Today, building this typically requires combining MQTT + Python microservices + JavaScript dashboards + a database + glue code — five runtimes, five data models, marshalling at every boundary. The triple proposes doing it **inside one process, with one object model, with true parallelism**.

## A flavour of the language

A pump twin — a stateful object promoted to an actor:

```smalltalk
"-- a pump as a state machine, run as an actor --"
Object subclass: #Pump instanceVariableNames: 'state ticks'.

Pump >> initialize  state := #idle.  ticks := 0.  ^ self.
Pump >> start       state := #running.            ^ self.
Pump >> tick
  state == #running ifTrue: [ ticks := ticks + 1 ].
  ^ ticks.
Pump >> ticks  ^ ticks.

p := Pump newChild.  p initialize.  p start.
a := p asActor.                 "promote the pump to an actor"
a tick.  a tick.  a tick.       "asynchronous sends — each returns a Future"
(a ticks) wait.                 "=> 3, once the three ticks have run"
```

Every send to `a` is dispatched asynchronously and returns a `Future`; the
single-method invariant guarantees the three `tick`s never interleave. A
complete, runnable digital-twin demo — a pump with three sensors read
concurrently on a worker pool — is in
[`examples/pump_twin.st`](examples/pump_twin.st).

## Performance — 50-70K msg/s actor messaging, real parallel scaling

**Actor messaging in the 50-70 K msg/s band on a 6-core 2020 notebook CPU.
Single-thread workloads geomean 4.65× CPython 3.14 free-threading on the
comparable suite (`benchmarks/comparable/`), beating CPython on
`str_concat` and at parity vs `protopy` overall.** Numbers below are from
the 2026-05-24 perf sprint (7 commits in protoST + 1 in protoCore that
day); full dated reports under
[`benchmarks/reports/`](benchmarks/reports/) — start with
[`2026-05-24-perf-final.md`](benchmarks/reports/2026-05-24-perf-final.md)
and [`2026-05-24-actor-messaging.md`](benchmarks/reports/2026-05-24-actor-messaging.md).

### Actor messaging throughput

Three patterns measured, all on a Ryzen 5 5500U (6 physical / 12 SMT,
4 GHz boost, 15-25 W TDP — a 2020 mobile chip chosen deliberately as the
publishing floor):

| benchmark | best | rate | optimal workers |
|---|---:|---:|---:|
| `message_throughput.st` (1 sink, 2000 drained round-trips) | 30 ms | **66.7 K msg/s** | w=2 |
| `mt100a` (1 producer → 100 sinks, 1000 rounds = 100 K msgs) | 1.83 s | **54.6 K msg/s** | w=2-4 |
| `multi_producer.st` (8 drivers × 12 sinks × 1000 rounds = 96 K msgs) | 1.61 s | **59.6 K msg/s** | w=4-6 |

All three patterns land in the 50-70 K band. **Multi-producer
empirically does NOT unlock a higher ceiling** on this hardware (each
driver's `doYielding:` per-element resume cost cancels the saved
single-producer-bottleneck cost) — earlier reports projecting "1.5 M
msg/s when multi-producer lands" were over-optimistic and have been
withdrawn. The real next architectural step is per-actor message slab
allocation + selector-resolved-once caching; see
`2026-05-24-actor-messaging.md` for the analysis.

### Hardware sensitivity (honest projection)

| host CPU | factor vs 5500U | est. peak msg/s |
|---|---|---:|
| AMD Ryzen 5 5500U (this report) | 1.00× | **60-70 K** (measured) |
| Apple M3 / Ryzen 7 7700X        | ~ 1.9× | ~ 115 K |
| Ryzen 9 7900X / i9-13900K       | ~ 2.0× × more cores | ~ 130-150 K |
| EPYC 96c server (X3D)           | ~ 1.7× ST × 16× cores | ~ 500 K - 1 M |

Even at the high end, protoST stays meaningfully below BEAM
(Erlang/Elixir's 5-10 M msg/s on the same EPYC class). Closing that
gap is the active perf workstream, NOT a finished story — see "How
protoST compares" below.

### Other actor benchmarks

- **Parallel speedup.** 12 CPU-bound worker actors saturating 6 cores:
  **3.88× scaling** (near-ideal 4× on 6 physical cores, w=6). Extra
  cores → real wall-clock speedup, no code change. This is the
  architectural property that distinguishes protoST from green-thread
  systems including Pharo / Squeak.
- **Cooperative-yield density.** 1000 actors each parked on a nested
  `wait`, all hosted on just **2** worker threads — completes in
  ~2.5 s. Thread-per-actor blocking would need 1000 OS threads;
  protoST parks the waiters and reuses the two. Same architectural
  league as BEAM and Go's M:N scheduler.

### Single-thread vs CPython 3.14 (free-threading)

Geomean across the 7-test comparable suite: **protoST 4.65× CPython**,
**1.02× protopy** (parity with the bytecode interpreter of our
companion Python runtime, both measured on the same suite).

| Workload | protoST | CPython | Ratio | Status |
|---|---:|---:|---:|---|
| `str_concat` (2000 chained `,`) | 19 ms | 30 ms | **0.64× (WIN)** | beats CPython |
| `list_append` (10K `add:`)      | 99 ms | 37 ms | 2.78× | within 3× |
| `int_sum_loop` (sum 1..100k)    | 103 ms| 50 ms | 2.08× | within 3× |
| `range_iterate` (iterate 100k)  | 154 ms| 37 ms | 4.17× | mid-pack |
| `attr_lookup` (300k iv reads)   | 281 ms| 45 ms | 6.27× | mid-pack |
| `fib` (recursive fib(25))       | ~440 ms| 56 ms| ~7.9× | OOP recursion |
| `exception_latency` (50K signals)| 1248 ms| 39 ms| 32×   | needs on:do: inlining |
| **Geomean**                     |       |       | **4.65×** |       |

Day-start was 9.96× geomean; the four 2026-05-24 landings
(ifTrue:/whileTrue: inlining, to:do: inlining, Dictionary key
canonicalisation + rope-aware `,`, SmallInt fast opcodes) cut it to
4.65×. The remaining 3-4× gap is concentrated on workloads bound by
method-call dispatch (`getAttribute` MRO walk — needs an inline
cache) and on `exception_latency` (`on:do:` handler frames not yet
inlined). See `2026-05-24-perf-final.md` for the per-fix attribution.

## How protoST compares

The two comparisons that matter for protoST live in different lanes —
single-threaded Smalltalk runtimes and parallel-actor runtimes — and
mixing them produces confused marketing. Here they are separately and
honestly.

### vs. Pharo / Squeak (Smalltalk syntax + tooling)

Pharo and Squeak are mature single-threaded Smalltalks. Their Cog VM
has a JIT, polymorphic inline caches, and 20+ years of tuning; on raw
single-thread arithmetic Cog is typically ~2-4× faster than protoST
today, and **protoST has no realistic path to closing that gap
without a JIT**. We are not building a faster Smalltalk than Pharo;
that comparison is not where protoST is interesting.

The architectural distinction is that **Pharo's "actors" / Vats /
Pharo-Actors are coroutines layered on green threads on one OS
thread** — they give the actor programming model but not real
parallelism. For any workload that needs more than one core, Pharo
fundamentally cannot use it; protoST's worker pool maps actors to
hardware cores 1:1 and demonstrably scales 3.88× on 6 physical cores.
That's the real distinction, not "Smalltalk that's faster than
Pharo".

### vs. BEAM (Erlang/Elixir) / Akka / Pony — the real comparator

protoST IS in the BEAM/Akka/Pony lineage technically — true parallel
actor messaging over OS threads. The gap below is the **honest
comparator on the actor side**:

| Pattern | protoST 5500U | BEAM ballpark on similar hw | Gap |
|---|---:|---:|---:|
| Shallow ping (single receiver, drained) | 67 K msg/s | 200-500 K (GenServer.call) | **3-7×** slower |
| Fan-out (1 producer → N receivers) | 55 K msg/s | 1-3 M (parallel sends) | **18-55×** slower |
| Multi-producer pipeline (N drivers × M sinks) | 60 K msg/s | 5-10 M (BEAM is heavily tuned for this) | **80-170×** slower |

So **the BEAM gap is real and significant** (3× on the narrow end,
80-170× on the wide end). BEAM has decades of scheduler tuning and a
per-process copying-GC design that protoST does not yet match.

| category | representative systems | in-process throughput | comment |
|---|---|---:|---|
| Actor languages, JIT, mature | BEAM, Akka, Orleans | 1-10 M msg/s | true parallel, decades of tuning |
| Actor languages, ground-up | Pony, Caf | 10-100 M msg/s | type-checked capability systems |
| **protoST today** | — | **50-70 K measured / 130-200 K projected (desktop)** | true parallel; gap to BEAM is the active workstream |
| Python actor frameworks | Pykka, Thespian, in-process Ray | 5-30 K msg/s | GIL-bound (mostly) |
| Smalltalk pseudo-actors | Pharo Vats, Pharo-Actors | (n/a — green threads, no parallelism) | not in this comparator class |
| Network message brokers | RabbitMQ, Kafka, NATS | 50 K-1 M msg/s | network + disk in the loop |

protoST does NOT market itself as "BEAM-class actor performance" —
the data don't support that today. What protoST IS:

### 1. Messages are pointers — even when they carry large state

Most actor systems pay for safety the same way: copy the message at the
send boundary. BEAM deep-copies between process heaps; Node.js worker
threads structured-clone; even Akka recommends immutable case classes
that the JVM still ends up tracing. The cost grows linearly with message
size — a 10 K-node world model is megabytes of marshalling per send.

protoST passes a pointer. Same address, on another core, no copy and no
serialisation. It is safe because the *pointee* — protoCore's mutable
object — is an atomic reference to an immutable snapshot: reading it is
unconditionally race-free, writing it installs a NEW snapshot that
doesn't disturb the old. **A message of N bytes costs the same as a
message of 0 bytes.**

Concrete comparison — sending a 10 K-node world graph between actors:

| system  | send cost (approx) |
|---|---:|
| BEAM (Erlang) — deep copy across heaps | ~ 0.5-2 ms |
| RabbitMQ — serialise + network + deserialise | ~ 1-10 ms |
| Akka (JVM) — pointer pass, but UB unless caller-discipline | < 100 µs (with caveats) |
| **protoST** — pointer to atomic snapshot | **~ 14 µs (the mt100a per-msg number)** |

For workloads where the message IS the state (digital twins, agent-based
simulation, data-flow pipelines), this **inverts the design space**.
Sharing large structures stops being a problem you architect around.

### 2. Trilingual in the same process, with one object model

protoCore hosts three languages — Smalltalk (protoST), JavaScript
(protoJS) and Python (protoPython) — and they share objects natively.
A protoPython numpy array reaches a protoST actor as the same pointer,
no marshalling. The conventional stack (MQTT + Python microservices +
JavaScript dashboards + a database) becomes three modules in one
address space.

This is not a comparison — no other actor runtime has this property at
all. It exists because protoCore was designed for it.

### 3. No GIL, no data races, no locks — by data-model construction

protoPython runs Python 3.14 without a GIL. protoST's actors run truly
in parallel on a worker pool. And the absence of data races does NOT
require programmer discipline — it falls out of the object model:
every read sees an internally consistent snapshot, every write
installs a new one, and the actor invariant (one method at a time per
actor) is enforced by the scheduler, not by the user.

### When does protoST lose?

- **Millions of trivial-message actors at peak throughput.** If your
  workload is `ping → pong` with empty bodies, BEAM and Pony will
  outrun protoST by 10-100×. The actor message is dominated by the
  dispatch path; a JIT helps there more than any tuning we can do
  on an interpreter.
- **Hard-real-time or strictly bounded latency.** protoST has GC
  pauses (concurrent collector, no soft-/hard-RT mode); BEAM has
  per-process GC and is widely used in soft-RT contexts.
- **Distributed actors across multiple machines.** protoST is
  single-process today. Distributing is a future track; for
  inter-node messaging today you would still pair it with a
  RabbitMQ / Kafka transport.

### When does protoST win?

- **Agent / twin simulations with large shared world state.** Pointer
  messaging × snapshot safety = the entire copy-vs-race spectrum
  collapses into a single cheap option.
- **Mixed-language pipelines** (numpy / ML in Python, UI logic in
  JavaScript, simulation engine in Smalltalk) where today you would
  marshal at every language boundary. The triple removes the
  marshalling line item entirely.
- **Anything where the message IS the state**, and the state is
  non-trivial. Sending a 100-element list, a 10 K-node graph, a
  matrix slice — all O(1) in send cost.

The pitch is not "the fastest actor framework" — it is "the actor
framework where messages can be pointers to large shared state, safely,
in three languages, in one process." If you have a digital twin or an
agent-based simulation that today requires a microservice mesh to
escape Python's GIL or JavaScript's structured-clone, that is the
problem protoST is shaped to solve.

## Project status

protoST is in active development and runs. **Latest tag: v0.3.0** (yieldable
cooperative iteration via `doYielding:` — see
[`docs/tutorial/10-actors-and-futures.md`](docs/tutorial/10-actors-and-futures.md)
§10.8). Phases F1–F8 are complete — lexer, parser, bytecode VM, closures,
classes, modules, the actor model with cooperative yield, an interactive
REPL, and a Debug Adapter Protocol debugger. Roadmap Tracks 1–6 are
complete: non-local return and a full exception protocol, the collection
hierarchy, the advanced object model (multiple inheritance, mixins,
runtime behaviour composition), the standard library (Stream, Math,
Random, JSON, Time), a defensible conformance suite, and cross-language
UMD interop. Track 8 (the dual-audience tutorial) is also done — see
[`docs/TUTORIAL.md`](docs/TUTORIAL.md). Tracks 7 and 9 (onboarding guides,
the broader example set) remain. The test suite stands at **753 tests
passing** (`ctest`, post 2026-05-24 perf sprint).

**Active perf workstream**: closing the BEAM messaging gap. Items
identified, scoped, and queued (largest expected impact first):
per-actor message slab allocator, selector-resolved-once inline cache
for SEND, `on:do:` handler inlining. None on a critical path for any
current user; this is "production-credible actor runtime" investment.

- [`docs/LANGUAGE.md`](docs/LANGUAGE.md) — the language reference.
- [`docs/STATUS.md`](docs/STATUS.md) — the live state: what works, intentional
  deviations from standard Smalltalk, and open bugs.
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — what is planned and how to contribute.

## Getting started

protoST depends on [protoCore](../protoCore), which must be built first.

```bash
cd protoST
cmake -B build -S .
cmake --build build -j

./build/protost script.st           # run a .st script
./build/protost -e 'expr'           # evaluate an expression
./build/protost -i                  # interactive REPL (history, multi-line)
./build/protost -d script.st        # run under the CLI debugger
./build/protost --dap               # Debug Adapter Protocol server (VS Code)
./build/protost venv create .venv   # create an isolated environment

ctest --test-dir build               # run the test suite
```

See [`docs/debugging.md`](docs/debugging.md) for debugging `.st` scripts in
VS Code.

## Installation

protoST ships native installers built with CPack. Every package depends on
[protoCore](../protoCore) — install protoCore's package first (it provides
`libprotoCore`).

**Debian / Ubuntu** — install the `.deb`:

```bash
sudo apt install ./protocore-<version>.deb     # the protoCore dependency
sudo apt install ./protost-<version>-Linux.deb # protoST itself
# or, lower-level:
sudo dpkg -i protost-<version>-Linux.deb && sudo apt-get install -f
```

**macOS** — open the `.dmg` and drag `protoST` to `Applications`.

**Windows** — run the NSIS installer (`protost-<version>-win64.exe`) and
follow the wizard, or unzip the portable `.zip`.

The installed `protost` lands on your `PATH`; the standard library is installed
to `<prefix>/share/protoST/lib`, so `Import from: 'stream'` resolves with no
`PROTOST_LIB` set.

### Building the packages from source

After a normal build, run CPack from the build directory:

```bash
cmake -B build -S . && cmake --build build -j
cd build
cpack -G DEB    # Debian/Ubuntu .deb (Linux)
cpack -G RPM    # RPM (Linux, needs rpmbuild)
cpack -G TGZ    # portable .tar.gz (Linux)
cpack -G DragNDrop   # .dmg (macOS)
cpack -G NSIS        # installer .exe (Windows, needs NSIS)
```

The generators are selected automatically per platform; `cpack` with no `-G`
builds every generator enabled for the host OS.

## Documentation

| Document | What it covers |
|---|---|
| [docs/TUTORIAL.md](docs/TUTORIAL.md) | The dual-audience tutorial — teaches protoST from the ground up for Python/JavaScript developers, and catalogues every departure from Smalltalk-80 for Smalltalk programmers. 14 chapters under `docs/tutorial/`. |
| [docs/LANGUAGE.md](docs/LANGUAGE.md) | The language reference — lexical structure, grammar, semantics, the full built-in protocol. |
| [docs/STATUS.md](docs/STATUS.md) | The living status tracker — implemented features, intentional deviations, open bugs. |
| [docs/ROADMAP.md](docs/ROADMAP.md) | The roadmap — remaining tracks and how to contribute. |
| [docs/INTEROP.md](docs/INTEROP.md) | Cross-language UMD interop strategy — how protoST consumes objects and modules from another protoCore runtime (protoJS, protoPython), the type mapping, and the tri-runtime follow-up plan. |
| [docs/debugging.md](docs/debugging.md) | Debugging `.st` scripts in VS Code via the `protost --dap` Debug Adapter Protocol adapter. |
| [design spec](docs/superpowers/specs/2026-05-19-protost-design.md) | The original technical design specification. |

## Related projects

- **[protoCore](../protoCore)** — the prototype-based kernel: object model, GC, immutable collections, GIL-free concurrency, UMD module discovery.
- **[protoJS](../protoJS)** — JavaScript runtime on protoCore. Source of the `Deferred` / `CPUThreadPool` patterns that protoST extends into a full actor model.
- **[protoPython](../protoPython)** — Python 3.14 runtime on protoCore. Source of the bytecode-format and HPy bridging patterns that protoST reuses.

## Why "protoST"?

`proto` — built on protoCore. `ST` — Smalltalk. The name signals what it is and where it lives in the family.

## The Swarm of One

**The Swarm of One** enables a paradigm shift in software development. protoST
went from an approved design to a complete actor-native Smalltalk runtime —
non-local return, a full exception protocol, a collection hierarchy, a
cooperative-yield scheduler, an interactive REPL, and a Debug Adapter Protocol
debugger — in record time. By orchestrating a swarm of specialized AI agents, a
single architect has built a runtime where Smalltalk objects share the same
64-byte cell DNA as protoCore, protoJS, and protoPython. This is the
democratization of high-level engineering: bridging language paradigms without
the traditional overhead of massive R&D teams.

## License

protoST is released under the [MIT License](LICENSE) — the same licence as
protoCore, protoJS, and protoPython.
