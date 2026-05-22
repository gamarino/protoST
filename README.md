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

## Performance

protoST ships a benchmark suite ([`benchmarks/`](benchmarks/)) with two
families: the protoPython core workloads translated to idiomatic Smalltalk
(same algorithm, same N — directly comparable against CPython) and
actor-model benchmarks that have no Python counterpart.

**Single-thread, vs CPython 3.14** — protoST is a young runtime and is slower
on raw arithmetic; the suite reports this honestly:

| Workload | protoST | CPython | Ratio |
|---|---:|---:|---:|
| `int_sum_loop` (sum 1..100k) | ~569 ms | ~30 ms | ~19× slower |
| `list_append` (10k appends) | ~107 ms | ~29 ms | ~3.7× slower |
| `range_iterate` (iterate 100k) | ~696 ms | ~31 ms | ~23× slower |

Recursive `fib(25)` and the O(N²) `str_concat` are far slower still — message
dispatch and immutable-string concatenation are not yet optimised. Raw
single-thread speed is **not** where protoST competes.

**The actor model is the differentiator** — and protoPython has nothing to
compare against here:

| Actor benchmark | Result |
|---|---|
| **Parallel speedup** | 12 CPU-bound worker actors run **~1.9× faster** on the full worker pool than pinned to one worker (`PROTOST_WORKERS=1`) — extra cores, no code change. |
| **Cooperative-yield scaling** | **1,000** actors, each parked on a nested `wait`, all hosted on just **2** worker threads. Thread-per-actor blocking would need 1,000 OS threads; protoST parks the waiters and reuses the two. |
| **Message throughput** | **~7,700 messages/second** — 2,000 drained round-trip sends to one actor (enqueue → schedule → execute → settle the `Future` → wake the sender). |

The cooperative-yield number is the headline: a thousand suspended actors on
two OS threads is a structural capability, not a tuning result. See the full
dated report — [`benchmarks/reports/2026-05-21-baseline.md`](benchmarks/reports/2026-05-21-baseline.md)
— for the complete table and the host details.

## Project status

protoST is in active development and runs. Phases F1–F8 are complete — lexer,
parser, bytecode VM, closures, classes, modules, the actor model with
cooperative yield, an interactive REPL, and a Debug Adapter Protocol debugger.
Roadmap Tracks 1–6 are complete: non-local return and a full exception
protocol, the collection hierarchy, the advanced object model (multiple
inheritance, mixins, runtime behaviour composition), the standard library
(Stream, Math, Random, JSON, Time), a defensible conformance suite, and
cross-language UMD interop. Track 8 (the dual-audience tutorial) is also done —
see [`docs/TUTORIAL.md`](docs/TUTORIAL.md). Tracks 7 and 9 (onboarding guides,
the broader example set) remain. The test suite stands at ~700 tests.

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
