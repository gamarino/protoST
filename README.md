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

A minimal digital twin written in protoST:

```smalltalk
"-- pump.st --"
Object subclass: #Pump
  instanceVariableNames: 'state pressure temperature serial'.

Pump >> initialize
  state := #operating.
  pressure := 0. temperature := 0.

Pump >> sensorReading: aReading
  state == #operating ifTrue: [ ^ self handleOperating: aReading ].
  state == #warning   ifTrue: [ ^ self handleWarning:   aReading ].
  state == #shutdown  ifTrue: [ ^ self handleShutdown:  aReading ].

Pump >> handleOperating: aReading
  pressure := aReading pressure.
  temperature := aReading temperature.
  (pressure > 80 or: [ temperature > 90 ])
    ifTrue: [ state := #warning. self notifyWarning ].

Pump >> handleWarning: aReading
  (aReading pressure > 95) ifTrue: [ state := #shutdown. self emergencyStop ].
```

And a sensor-batch fan-out across a fleet of pumps:

```smalltalk
"-- ingest a batch of readings into a fleet of twins --"
ingestBatch: aBatch into: aFleet
  | futures |
  futures := aBatch collect: [:reading |
      (aFleet pumpForSerial: reading pumpId) sensorReading: reading ].
  ^ Future whenAll: futures.   "resolves when every twin has processed its event"
```

Each `(aFleet pumpForSerial: id)` is an actor proxy. The `sensorReading:` send is asynchronous and returns a Future. `whenAll:` produces a single Future that resolves when every twin in the batch has finished. The scheduler runs as many of them concurrently as cores allow.

## Project status

> ⚠️ **Early development.** The design specification is approved, implementation has not yet started. The repository currently contains only the design documents.

See the [design specification](docs/superpowers/specs/2026-05-19-protost-design.md) for the complete technical specification. Implementation is planned in nine phases (F1–F9), summarised in § 13 of the spec.

## Getting started

> Build instructions will be added during F1. The runtime depends on [protoCore](../protoCore) which must be built first.

Roughly:

```bash
cd protoST
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j

# Once F1 is complete:
./protost venv create .venv         # create an isolated env (Python-venv style)
source .venv/bin/activate
./protost -i                        # REPL with multiline, history, completion
./protost script.st                 # run a .st script as principal module
./protost -d script.st              # run under the CLI debugger (from F2)
```

## Documentation

| Document | What it covers |
|---|---|
| [DESIGN.md](DESIGN.md) → [spec](docs/superpowers/specs/2026-05-19-protost-design.md) | Full technical specification: architecture, object model, actor model, modules, debugger, venv, phases. |
| [CLAUDE.md](CLAUDE.md) | Project-specific instructions for Claude Code (to be populated). |

## Related projects

- **[protoCore](../protoCore)** — the prototype-based kernel: object model, GC, immutable collections, GIL-free concurrency, UMD module discovery.
- **[protoJS](../protoJS)** — JavaScript runtime on protoCore. Source of the `Deferred` / `CPUThreadPool` patterns that protoST extends into a full actor model.
- **[protoPython](../protoPython)** — Python 3.14 runtime on protoCore. Source of the bytecode-format and HPy bridging patterns that protoST reuses.

## Why "protoST"?

`proto` — built on protoCore. `ST` — Smalltalk. The name signals what it is and where it lives in the family.

## License

To be confirmed. The expectation is to mirror the licence chosen for protoCore.
