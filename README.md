# protoST

> **An actor-native Smalltalk for building digital twins on protoCore.**

protoST is a Smalltalk-80-inspired language runtime with a **first-class embedded actor model**, built on the [protoCore](../protoCore) kernel. It is the third member of the protoCore language triple — alongside [protoJS](../protoJS) (JavaScript) and [protoPython](../protoPython) (Python) — and it closes the technical demonstration that one prototype-based kernel can host three genuinely different OO paradigms (prototypes / classes / messages) without flattening them to a common denominator.

protoST's distinct contribution is putting **actors at the centre of the language**: any object can be promoted to an actor with `asActor`, every message sent to it is dispatched asynchronously and returns a `Future`, and an internal invariant guarantees that exactly one method of a given actor runs at a time. Tens of thousands of actors share a small worker pool through cooperative scheduling — an actor suspends transparently when it waits on a future, freeing its worker for someone else.

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

## Project status

protoST is in active development and runs. Phases F1–F8 are complete — lexer,
parser, bytecode VM, closures, classes, modules, the actor model with
cooperative yield, an interactive REPL, and a Debug Adapter Protocol debugger.
Roadmap Tracks 1–6 are complete: non-local return and a full exception
protocol, the collection hierarchy, the advanced object model (multiple
inheritance, mixins, runtime behaviour composition), the standard library
(Stream, Math, Random, JSON, Time), a defensible conformance suite, and
cross-language UMD interop. Tracks 7–9 (onboarding, tutorial, examples)
remain. The test suite stands at ~700 tests.

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

## Documentation

| Document | What it covers |
|---|---|
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
