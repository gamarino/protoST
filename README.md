# protoST

> **An actor-native Smalltalk for building digital twins on protoCore.**

protoST is a Smalltalk-80-inspired language runtime with a **first-class embedded actor model**, built on the [protoCore](https://github.com/numaes/protoCore) kernel. It is one of four language runtimes on protoCore — alongside [protoJS](https://github.com/gamarino/protoJS) (JavaScript), [protoPython](https://github.com/gamarino/protoPython) (Python) and [protoClojure](https://github.com/gamarino/protoClojure) (Clojure, early stage) — and it shows that one prototype-based kernel can host genuinely different OO paradigms (prototypes / classes / messages) without flattening them to a common denominator.

protoST's distinct contribution is putting **actors at the centre of the language**: any object can be promoted to an actor with `asActor`, every message sent to it is dispatched asynchronously and returns a `Future`, and an internal invariant guarantees that exactly one method of a given actor runs at a time. Many actors share a small worker pool through cooperative scheduling — an actor suspends transparently when it waits on a future, freeing its worker for someone else.

Three **priority bands** separate the data plane from the control plane: `asActor` is the default Medium, `asHighPriorityActor` jumps the queue for control messages (drain / reconfigure / shutdown), and `asLowPriorityActor` yields for background hygiene (telemetry, log flushes). The scheduler drains strict-priority — every High before any Medium before any Low — with the same single-method invariant in every band.

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
is safe. The message path — send, mailbox and ready queue — takes no locks.

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
3. **Concurrency by default.** Each twin is an actor; the cooperative-yield benchmark hosts 1000 actors on two worker threads, and adding twins needs no code change.

Together with the other protoCore runtimes, protoST is designed to form a platform for agent-based / discrete-event simulation:

| Need | Component |
|---|---|
| Many stateful concurrent agents | **protoST** (actor model on Smalltalk syntax) |
| Lock-free shared state across agents | **protoST** (`Atom` — optimistic-concurrency CAS cell) |
| Numerical models written in Python | **protoPython** |
| Operator dashboards, visualisation | **protoJS** (web tooling) |
| Real parallelism, no GIL | **protoCore** (kernel) |
| Immutable collections, structural sharing | **protoCore** (kernel) |
| Cross-language interop without marshalling | **UMD** (every module is a `ProtoObject`) |

Today, building this typically requires combining MQTT + Python microservices + JavaScript dashboards + a database + glue code — five runtimes, five data models, marshalling at every boundary. The protoCore runtimes aim to do it **inside one process, with one object model, with true parallelism**. protoST's side of cross-language interop is implemented; a live process hosting several runtimes at once is follow-up work (see [`docs/INTEROP.md`](docs/INTEROP.md)).

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

## Performance

All numbers below come from dated reports in
[`benchmarks/reports/`](benchmarks/reports/), measured on an AMD Ryzen 5 5500U
(6 physical cores / 12 threads, a 2020 notebook CPU). They are snapshots of the
code at the report date, not continuous measurements.

- **Actor messaging:** 50–70 K msg/s across three messaging patterns
  (2026-05-24, [`2026-05-24-actor-messaging.md`](benchmarks/reports/2026-05-24-actor-messaging.md)).
- **Single-thread code:** geomean **2.83× CPython 3.14.0's wall-clock time** on
  the seven-workload comparable suite (2026-05-24, after building protoCore
  with `NDEBUG`,
  [`2026-05-24-perf-after-protocore-ndebug.md`](benchmarks/reports/2026-05-24-perf-after-protocore-ndebug.md)).
- **Parallel scaling:** 3.88× on 6 physical cores for the CPU-bound
  `saturation_big` benchmark (2026-05-23,
  [`2026-05-23-performance.md`](benchmarks/reports/2026-05-23-performance.md)).

### Actor messaging throughput

From [`2026-05-24-actor-messaging.md`](benchmarks/reports/2026-05-24-actor-messaging.md)
(best of 3 runs):

| benchmark | best | rate | optimal workers |
|---|---:|---:|---:|
| `message_throughput.st` (1 sink, 2000 drained round-trips) | 30 ms | **66.7 K msg/s** | w=2 |
| `mt100a` (1 producer → 100 sinks, 1000 rounds = 100 K msgs) | 1.83 s | **54.6 K msg/s** | w=2-4 |
| `multi_producer.st` (8 drivers × 12 sinks × 1000 rounds = 96 K msgs) | 1.61 s | **59.6 K msg/s** | w=4-6 |

The `mt100a` row differs from the 71.9 K msg/s quoted for 0.2.0 in
[`CHANGELOG.md`](CHANGELOG.md) and [`docs/STATUS.md`](docs/STATUS.md). That
figure comes from
[`2026-05-23-performance.md`](benchmarks/reports/2026-05-23-performance.md),
added with the 0.2.0 release (commit `deb6b0d`); the table above was measured
at commit `b623448` for the 2026-05-24 report, which does not analyse the
difference.

All three patterns land in the 50-70 K band. **Multi-producer does not unlock
a higher ceiling** on this hardware: each driver's `doYielding:` per-element
resume cost cancels the saved single-producer bottleneck. An earlier
projection of 1.5 M msg/s for multi-producer
([`2026-05-23-performance.md`](benchmarks/reports/2026-05-23-performance.md))
is withdrawn. The report identifies per-actor message slab allocation and a
selector-resolved-once cache as the next architectural steps.

### Hardware sensitivity (projection)

[`2026-05-24-actor-messaging.md`](benchmarks/reports/2026-05-24-actor-messaging.md)
projects, without measuring, how the multi-producer peak would scale on other
CPUs:

| host CPU | factor vs 5500U | est. multi-producer peak |
|---|---|---:|
| AMD Ryzen 5 5500U | 1.00× | **60 K msg/s** (measured) |
| Apple M3 / Ryzen 7 7700X | ~1.9× | ~115 K msg/s |
| Ryzen 9 7950X (16-core desktop) | ~2.0× × more cores | ~200 K msg/s |
| EPYC 96-core server | per-core +60 %, 16× cores | ~500 K–1 M msg/s |

Even at the high end, protoST stays well below BEAM (Erlang/Elixir), which the
report puts at 5–10 M msg/s on the same server class. Closing that gap is open
work — see "How protoST compares" below.

### Other actor benchmarks

- **Parallel scaling.** `saturation_big` (32 actors × 50 messages × 5K CPU
  iterations each) runs 3.88× faster at `PROTOST_WORKERS=6` than at 1 on the
  6 physical cores (2026-05-23). Extra cores give real wall-clock speedup
  with no code change — the architectural property that distinguishes
  protoST from green-thread systems such as Pharo / Squeak. In the 2026-05-24
  NDEBUG report, `parallel_speedup.st` (12 CPU-bound worker actors) runs in
  359 ms with the full pool vs 665 ms with one worker (1.85×).
- **Cooperative-yield density.** 1000 actors, each parked on a nested
  `wait`, all hosted on **2** worker threads, complete in 1176 ms (2026-05-24
  NDEBUG report). Thread-per-actor blocking would need 1000 OS threads;
  protoST parks the waiters and reuses the two.

### Single-thread vs CPython 3.14

From [`2026-05-24-perf-after-protocore-ndebug.md`](benchmarks/reports/2026-05-24-perf-after-protocore-ndebug.md)
(2 warmup + 5 timed runs, median wall-clock; `Ratio` is protoST ÷ CPython,
greater than 1 means protoST is slower):

| Workload | protoST (ms) | CPython (ms) | protopy (ms) | Ratio (ST/CPy) |
|---|---:|---:|---:|---:|
| `int_sum_loop` | 38.7 | 39.6 | 22.4 | 0.98× |
| `fib` | 405.4 | 41.3 | 133.1 | 9.82× |
| `list_append` | 59.9 | 30.8 | 301.8 | 1.94× |
| `str_concat` | 34.2 | 44.7 | 540.4 | 0.77× |
| `attr_lookup` | 103.4 | 40.4 | 259.7 | 2.56× |
| `range_iterate` | 49.1 | 34.6 | 223.8 | 1.42× |
| `exception_latency` | 1280.6 | 45.4 | 702.7 | 28.18× |
| **Geomean** | | | | **2.83×** |

The `protopy` column is protoPython's bytecode interpreter at the time of the
report. protoST is faster than CPython on `str_concat` and at parity on
`int_sum_loop`; the largest gaps are recursive method dispatch (`fib`) and
exception signalling (`exception_latency`). The earlier reports from the same
day, including the per-fix attribution, are in `benchmarks/reports/`.

## How protoST compares

The two comparisons that matter for protoST live in different lanes —
single-threaded Smalltalk runtimes and parallel-actor runtimes — so they are
kept separate here.

### vs. Pharo / Squeak (Smalltalk syntax + tooling)

Pharo and Squeak are mature single-threaded Smalltalks. Their Cog VM has a
JIT, polymorphic inline caches and many years of tuning, and it is faster than
protoST on raw single-thread code; **protoST has no realistic path to closing
that gap without a JIT**. protoST is not trying to be a faster Smalltalk than
Pharo.

The architectural distinction is that **Pharo's "actors" / Vats /
Pharo-Actors are coroutines layered on green threads on one OS
thread** — they give the actor programming model but not real
parallelism. For any workload that needs more than one core, Pharo
cannot use it; protoST's worker pool runs actors on hardware cores and
scales 3.88× on 6 physical cores in `saturation_big`.

### vs. BEAM (Erlang/Elixir) / Akka / Pony

protoST is in the BEAM/Akka/Pony lineage technically — true parallel
actor messaging over OS threads. From
[`2026-05-24-actor-messaging.md`](benchmarks/reports/2026-05-24-actor-messaging.md)
(BEAM figures are ballpark estimates from that report, not measurements on
this host):

| Pattern | protoST 5500U | BEAM ballpark on similar hw | Gap |
|---|---:|---:|---:|
| Shallow ping (single receiver, drained) | 67 K msg/s | 200-500 K (GenServer.call) | **3-7×** slower |
| Fan-out (1 producer → N receivers) | 55 K msg/s | 1-3 M (parallel sends) | **18-55×** slower |
| Multi-producer pipeline (N drivers × M sinks) | 60 K msg/s | 5-10 M (BEAM is heavily tuned for this) | **80-170×** slower |

So **the BEAM gap is real and significant** (3× on the narrow end,
80-170× on the wide end). BEAM has decades of scheduler tuning and a
per-process copying-GC design that protoST does not match. protoST does not
claim BEAM-class actor performance. What it offers instead:

### 1. Messages are pointers — even when they carry large state

Most actor systems pay for safety the same way: copy the message at the
send boundary. BEAM deep-copies between process heaps; Node.js worker
threads structured-clone. The cost grows with message size.

protoST passes a pointer. Same address, on another core, no copy and no
serialisation. It is safe because the *pointee* — protoCore's mutable
object — is an atomic reference to an immutable snapshot: reading it is
race-free, and writing it installs a new snapshot that does not disturb the
old one. The send itself therefore copies nothing, whatever the message size.
The published benchmarks use small messages; a large-payload benchmark has
not been published yet.

For workloads where the message IS the state (digital twins, agent-based
simulation, data-flow pipelines), sharing large structures stops being a
problem you architect around.

### 2. One object model across language runtimes

Every protoCore runtime represents its values as protoCore objects built from
the same cell, so an object produced by protoJS or protoPython is an ordinary
object to protoST, and a message send to it follows the ordinary lookup path.
protoST's side of this is implemented and tested (Track 5, slice T5-a:
importing modules from a foreign UMD provider); a live process hosting
several runtimes at once is the follow-up described in
[`docs/INTEROP.md`](docs/INTEROP.md).

### 3. No GIL, no data races, no locks on the message path — by data-model construction

protoPython runs Python without a GIL. protoST's actors run truly
in parallel on a worker pool. And the absence of data races does NOT
require programmer discipline — it falls out of the object model:
every read sees an internally consistent snapshot, every write
installs a new one, and the actor invariant (one method at a time per
actor) is enforced by the scheduler, not by the user. A read-modify-write
on shared state still needs `Atom` (or single-actor ownership) to avoid lost
updates.

### When does protoST lose?

- **Peak throughput of trivial messages.** If your workload is
  `ping → pong` with empty bodies, BEAM outruns protoST by 3-170×
  depending on the pattern (table above). The message cost is dominated
  by the dispatch path, where a JIT helps more than interpreter tuning.
- **Hard-real-time or strictly bounded latency.** protoST has GC
  pauses (concurrent collector, no soft-/hard-RT mode); BEAM has
  per-process GC and is widely used in soft-RT contexts.
- **Distributed actors across multiple machines.** protoST is
  single-process today. Distribution is a future track; for
  inter-node messaging you would still pair it with a
  RabbitMQ / Kafka transport.

### When does protoST win?

- **Agent / twin simulations with large shared world state.** Pointer
  messaging plus snapshot safety removes the choice between copying
  and data races.
- **Anything where the message IS the state**, and the state is
  non-trivial. Sending a 100-element list, a 10 K-node graph or a
  matrix slice copies nothing.
- **Mixed-language pipelines**, once several protoCore runtimes share a
  process (see [`docs/INTEROP.md`](docs/INTEROP.md)): no marshalling at
  the language boundary.

protoST is not "the fastest actor framework"; it is an actor runtime where
messages can be pointers to large shared state, safely, on an object model
shared with other language runtimes. If you have a digital twin or an
agent-based simulation that today needs a microservice mesh to escape Python's
GIL or JavaScript's structured clone, that is the problem protoST is shaped
to solve.

## Project status

protoST is in active development. **Latest release tag: v0.3.0** (yieldable
cooperative iteration via `doYielding:` — see
[`docs/tutorial/10-actors-and-futures.md`](docs/tutorial/10-actors-and-futures.md)
§10.8). Phases F1–F8 are complete — lexer, parser, bytecode VM, closures,
classes, modules, the actor model with cooperative yield, an interactive
REPL, and a Debug Adapter Protocol debugger. Roadmap Tracks 1–11 are
complete, each with a `trackN-complete` tag: non-local return and a full
exception protocol, the collection hierarchy, the advanced object model
(multiple inheritance, mixins, runtime behaviour composition), the standard
library (Stream, Math, Random, JSON, Time), a conformance suite, cross-language
UMD interop, onboarding, the dual-audience tutorial
([`docs/TUTORIAL.md`](docs/TUTORIAL.md)), the example set, CPack packaging and
the benchmark suite.

Since v0.3.0 (see [`CHANGELOG.md`](CHANGELOG.md#unreleased)): call-form sends
and method declarations, class variables, the three actor priority bands, and
blocking OS calls wrapped in protoCore's unmanaged scope so they do not stall
the garbage collector.

The test suite has **833 `ctest` cases** (352 conformance, 42 examples, 12 CLI,
427 unit), counted with `ctest -N` on 2026-09-15, all passing. Open bugs are
tracked in [docs/STATUS.md](docs/STATUS.md).

**Open performance work**: closing the BEAM messaging gap. The 2026-05-24
reports identify, largest expected impact first: a per-actor message slab
allocator, a selector-resolved-once inline cache for SEND, and cheaper
`on:do:` handler frames.

- [`docs/LANGUAGE.md`](docs/LANGUAGE.md) — the language reference.
- [`docs/STATUS.md`](docs/STATUS.md) — the live state: what works, intentional
  deviations from standard Smalltalk, and open bugs.
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — what is planned and how to contribute.

## Getting started

protoST depends on [protoCore](https://github.com/numaes/protoCore), which must
be built first. By default the build looks for a protoCore checkout next to
protoST (`../protoCore`, library in `build/`, `build_release/` or
`build_check/`); pass `-DPROTO_CORE_PREFIX=<prefix>` to use an installed
protoCore instead.

```bash
git clone https://github.com/numaes/protoCore.git
git clone https://github.com/gamarino/protoST.git

cmake -S protoCore -B protoCore/build
cmake --build protoCore/build --target protoCore

cd protoST
cmake -B build -S .
cmake --build build -j

./build/protost script.st [args...]   # run a .st script
./build/protost -e 'expr'             # evaluate an expression and print the result
./build/protost -i                    # interactive REPL (history, multi-line)
./build/protost -d script.st          # run under the CLI debugger
./build/protost --dap                 # Debug Adapter Protocol server (VS Code)
./build/protost --dump-ast script.st  # parse a script and print its AST
./build/protost venv create .venv     # create an isolated environment
./build/protost --help                # usage (also -h)
./build/protost --version             # print the version (also -v)

ctest --test-dir build                # run the test suite
```

See [`docs/debugging.md`](docs/debugging.md) for debugging `.st` scripts in
VS Code.

## Installation

protoST can be packaged with CPack. No prebuilt packages are published; build
them from source as shown below. Every package depends on
[protoCore](https://github.com/numaes/protoCore) — install protoCore's package
first (it provides `libprotoCore`).

**Debian / Ubuntu** — install the `.deb`:

```bash
sudo apt install ./protoCore-<version>-Linux.deb # the protoCore dependency
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

The generators are selected per platform in `CMakeLists.txt` (Linux: DEB, RPM,
TGZ; macOS: DragNDrop; Windows: NSIS, ZIP); `cpack` with no `-G` builds every
generator enabled for the host OS. The `.deb` and `.tar.gz` packages have been
verified on Linux.

## Documentation

| Document | What it covers |
|---|---|
| [docs/TUTORIAL.md](docs/TUTORIAL.md) | The dual-audience tutorial — teaches protoST from the ground up for Python/JavaScript developers, and catalogues every departure from Smalltalk-80 for Smalltalk programmers. 14 chapters under `docs/tutorial/`. |
| [docs/LANGUAGE.md](docs/LANGUAGE.md) | The language reference — lexical structure, grammar, semantics, the full built-in protocol. |
| [docs/STATUS.md](docs/STATUS.md) | The living status tracker — implemented features, intentional deviations, open bugs. |
| [KNOWN_ISSUES.md](KNOWN_ISSUES.md) | Runtime hard edges that are not language design choices. |
| [docs/ROADMAP.md](docs/ROADMAP.md) | The roadmap — remaining tracks and how to contribute. |
| [docs/INTEROP.md](docs/INTEROP.md) | Cross-language UMD interop strategy — how protoST consumes objects and modules from another protoCore runtime (protoJS, protoPython), the type mapping, and the multi-runtime follow-up plan. |
| [docs/debugging.md](docs/debugging.md) | Debugging `.st` scripts in VS Code via the `protost --dap` Debug Adapter Protocol adapter. |
| [CHANGELOG.md](CHANGELOG.md) | Release notes and unreleased changes. |
| [benchmarks/README.md](benchmarks/README.md) | The benchmark suite and harness; dated reports live in `benchmarks/reports/`. |
| [Design specifications (archive)](docs/archive/design-specs/README.md) | Historical design documents, including the original technical design; not maintained. |

## Related projects

Four language runtimes (protoJS, protoPython, protoST, protoClojure) and protoCpp's C++ examples are built on protoCore.

| Project | Role | Repository |
|---|---|---|
| protoCore | C++20 object model and runtime kernel: immutable structures, concurrent GC, GIL-free threads | https://github.com/numaes/protoCore |
| protoJS | JavaScript runtime on protoCore | https://github.com/gamarino/protoJS |
| protoPython | Python 3 runtime (protopy) and ahead-of-time compiler (protopyc) on protoCore | https://github.com/gamarino/protoPython |
| protoST | Smalltalk-inspired actor language on protoCore | https://github.com/gamarino/protoST |
| protoClojure | Clojure dialect on protoCore (early stage) | https://github.com/gamarino/protoClojure |
| protoCpp | Examples and benchmarks using protoCore directly from C++ | https://github.com/gamarino/protoCpp |

Its actor model was informed by protoJS's `Deferred` / `CPUThreadPool` design
(see the references of the
[original design specification](docs/archive/design-specs/2026-05-19-protost-design.md)).

## Why "protoST"?

`proto` — built on protoCore. `ST` — Smalltalk. The name signals what it is and where it lives in the family.

## The Swarm of One

protoST is designed and maintained by a single architect, Gustavo Marino,
working with AI coding agents that draft code, tests and documentation under
human review. As of 2026-09-15 the repository has three release tags
(v0.1.0 to v0.3.0) and 786 `ctest` cases.

## License

Copyright (c) 2023-2026 Gustavo Marino. Released under the MIT License; see [LICENSE](LICENSE).
