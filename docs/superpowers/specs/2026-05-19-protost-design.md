# protoST — Design Specification

**Date:** 2026-05-19
**Status:** Draft, pending user review before passing to implementation planning
**Next step:** User reviews this document; once approved, generate implementation plan via superpowers:writing-plans

---

## 1. Overview

> **protoST — an actor-native Smalltalk for building digital twins on protoCore.**

protoST is a Smalltalk-80-inspired language runtime built on protoCore, alongside protoJS and protoPython. The triple is the technical demonstration that one prototype-based kernel can host three genuinely different OO paradigms (prototypes / classes / messages) without flattening them to a common denominator.

protoST's distinct contribution is a **first-class embedded actor model** with cooperative scheduling over a worker pool, materialising the message-passing vision Alan Kay associated with the original Smalltalk. The actor model is not a library nor an addon: every object can be promoted to an actor with `asActor`, and the entire concurrency story of the language is built around it.

**Primary target use case: digital twins.** A digital twin is, structurally, a collection of finite state machines (FSMs) that represent real-world entities — a pump, a vehicle, a patient, a substation — exchanging events. The actor model is a near-perfect fit: each twin is an actor encapsulating state and behaviour; events are messages; cooperative scheduling allows tens of thousands of twins on a few cores; UMD interop pulls numerical models from protoPython (numpy, scipy, ML) and dashboards from protoJS into the same address space without marshalling. § 6.8 elaborates on the actor-as-FSM equivalence and why it makes the model fit the use case.

The runtime is a bytecode interpreter directly over protoCore primitives, with **the absolute minimum amount of glue code** above protoCore. Every architectural choice in this spec is optimised for three constraints in this order: (1) minimal decoration over protoCore, (2) maximum performance, (3) cleanest fit for the actor / digital-twin use case.

## 2. Goals and non-goals

### Goals

- A Smalltalk-80 dialect (minimum core + extensions) executable from `.st` source files.
- File-based module system in the style of Python, integrated with protoCore's Unified Module Discovery (UMD), so a `.st` file can `Import from:` any module reachable through any UMD provider (native C++, `.py` from protopyc, `.js` from protojs).
- First-class `Future` and `Actor` types. Any object can be promoted to an actor with `asActor`; sends to the actor are dispatched asynchronously and return `Future`s. Inside an actor, exactly one method runs at a time (single-method invariant).
- Cooperative scheduling: a fixed worker pool (size ≈ number of cores) runs many lightweight actors; an actor suspends transparently when it `wait`s on a `Future`, freeing its worker for another actor.
- Python-like interactive REPL: multiline input, history, completion, meta-commands.
- C++ bytecode VM with performance comparable to protoJS / protoPython.

### Non-goals (Phase 1)

- Image-based persistence (`ChangeSet`, `become:`, world snapshots). The project is strictly file-based.
- Classic metaclass hierarchy (Smalltalk-80's `Metaclass` / `Class class class` recursion). "Class-side methods" are installed on the class's prototype but no separate metaclass object is materialised.
- Static AOT compiler equivalent to `protopyc` / a `.so` packager. Considered for a later phase.
- Integrated sampling / allocation profiler. Considered for a later phase. (A CLI debugger ships from F2; DAP support is F8 — see § 11 and § 13.)
- Akka-style supervision trees with restart strategies. The default policy is "future rejected, actor stays alive"; supervisors can be added later as a pure-Smalltalk library without runtime changes.

## 3. Architectural principles

### 3.1 Minimal decoration over protoCore

The runtime adds the smallest possible layer above protoCore. Concretely:

- **No `TypeBridge` layer.** protoJS and protoPython have one because their frontend (QuickJS, CPython) brings its own type system. protoST has no such constraint: parser literals are emitted directly as `ProtoInteger`, `ProtoString`, etc.
- **No `STModuleAdapter`.** A module obtained from `getImportModule` is already a `ProtoObject` with attributes; access like `m Counter` is `getAttribute(m, #Counter)` via `doesNotUnderstand:`.
- **No custom symbol cache class.** Frequently-used selectors are interned at bootstrap as C++ statics via `ProtoString::createSymbol(nullptr, name)` (perpetual allocation, mechanism A in `protoCore/DESIGN.md`).
- **No custom inline cache.** protoCore's `ProtoContext` already provides attribute-lookup caching that auto-inherits across thread boundaries. The bytecode emits plain `SEND`, and dispatch performance comes from protoCore.
- **No dedicated `WAIT` opcode.** `Future>>wait` is a primitive method (`<primitive: #FutureWait>`); the `SEND` opcode already executes primitives.

### 3.2 GC-bridging discipline (heritage from protoJS / protoPython)

protoST inherits the rules in `protoCore/DESIGN.md` § "Keeping ProtoObjects alive across allocation boundaries the GC cannot see":

- **Mechanism A — NULL `ProtoContext` allocation** for vocabulary (selectors, prototypes, cached literals like `nil` / `true` / `false`).
- **Mechanism B — `ProtoRootSet`** for transient async pinning: any `Future` that is pending and any captured callback are pinned in the runtime's root set `"protoST-async"` from creation/registration until resolution/invocation. A single `ProtoRootSet` per `STRuntime` instance.

### 3.3 No `std::vector` for execution state (rule absolute, copied from protoJS § 1.3a)

The bytecode interpreter never stores operand stack or local variables in a C++ container. Both are `ProtoObject` storage (operand stack as a `ProtoList`, locals as automatic slots and/or a `ProtoSparseList` keyed by symbol). This rule has two consequences:

1. The GC sees the entire execution state.
2. Suspending an actor mid-method is trivial: snapshot three pointers (PC, operand stack, locals) into the actor's `__suspendedFrame__` attribute.

## 4. Architecture (layers)

```
┌──────────────────────────────────────────────────────────┐
│                Smalltalk-80 source (.st)                 │
│  - File = module                                         │
│  - Class declarations, methods, top-level expressions    │
└─────────────────────────┬────────────────────────────────┘
                          │
                          ▼
┌──────────────────────────────────────────────────────────┐
│                     Parser → AST                         │
│  Hand-written recursive-descent parser in C++.           │
│  Standard ST-80 grammar + module declarations.           │
└─────────────────────────┬────────────────────────────────┘
                          │
                          ▼
┌──────────────────────────────────────────────────────────┐
│                  Compiler → Bytecode                     │
│  - 2-byte instruction format (matches protopyc)          │
│  - Closure analysis for blocks                           │
│  - Emits ProtoBytecodeModule (constants + bytes)         │
└─────────────────────────┬────────────────────────────────┘
                          │
                          ▼
┌──────────────────────────────────────────────────────────────┐
│              protoST Runtime (libprotoST.so)                 │
│  ┌──────────────────┐  ┌──────────────────────────────────┐  │
│  │ ExecutionEngine  │  │ ActorRuntime                     │  │
│  │ (bytecode loop)  │  │ - Mailbox CAS                    │  │
│  │                  │  │ - Scheduler (worker pool, ready) │  │
│  │                  │  │ - Future state + waiters         │  │
│  └──────────────────┘  └──────────────────────────────────┘  │
│  ┌──────────────────┐  ┌──────────────────────────────────┐  │
│  │ STModuleProvider │  │ Primitives                       │  │
│  │ (UMD provider)   │  │ (int/str/list/block/future)      │  │
│  └──────────────────┘  └──────────────────────────────────┘  │
└─────────────────────────┬────────────────────────────────────┘
                          │
                          ▼
┌──────────────────────────────────────────────────────────────┐
│                          protoCore                           │
│  ProtoSpace · ProtoContext · ProtoObject · ProtoThread       │
│  ProtoList / ProtoSparseList / ProtoString / ProtoSet        │
│  ProtoRootSet · GC · SymbolTable · UMD ProviderRegistry      │
└──────────────────────────────────────────────────────────────┘
```

## 5. Object model and binding to protoCore

### 5.1 Classes as prototypes

A Smalltalk class is a `ProtoObject` acting as a Lieberman-style prototype. Instances are created via `prototype->newChild(ctx, /*mutable=*/true)`. The class name lives in attribute `__name__`. The superclass of *instances* is the class prototype itself; additional mixin parents on instances are attached with `addParent(ctx, otherProto)`.

**Class-side methods** (`Counter class >> startingAt: n`) need to be reachable from `Counter` itself but **not** from instances of `Counter` (otherwise an instance could call `instance startingAt: 5` and the meaning of "class side" would dissolve). The minimal mechanism that achieves this without rebuilding Smalltalk's classic metaclass tower:

- Each user class `C` has a single private parent prototype `C_classside` (created lazily on first class-side method definition).
- Instance-side methods are installed on `C` itself (and therefore inherited by instances created via `C newChild`).
- Class-side methods are installed on `C_classside`. `C_classside` is added as a parent of `C` only — not of `C`'s children — via `C->addParent(ctx, C_classside)`.
- The result: `Counter new` finds `new` via `Counter → Counter_classside`. An instance does not see `Counter_classside` (it is not a parent of the instance) and so does not inherit `startingAt:`.

This single-level "classside parent" is intentionally simpler than Smalltalk-80's `Metaclass class class` recursion (which is the part listed as a non-goal): there is no metaclass-of-the-metaclass, and `Counter class class class` returns `Class` and stops.

### 5.2 Method dispatch

Compiled methods are `ProtoMethod` objects installed on the prototype via `setAttribute(ctx, selector, methodObj)`. Selectors are perpetual symbols.

The `SEND` opcode is the dispatch path. Pseudocode:

```
SEND <selector_idx>:
  args = pop argc            // varies by selector arity
  recv = pop
  if recv.classPrototype is actorProto:
      future = ActorRuntime::enqueue(recv, selector, args)
      push future
      continue
  method = recv.getAttribute(selector)    // protoCore caches this
  if method is nil:
      method = recv.getAttribute(#doesNotUnderstand:)
      args = [Message new selector: selector arguments: args]
  invoke method with (recv, args)
```

The check `recv.classPrototype == actorProto` is a single pointer comparison; the branch is well-predicted because actor sends and non-actor sends rarely interleave at a given call site.

### 5.3 Mapping language literals to protoCore types

| ST literal | Language type | protoCore representation |
|---|---|---|
| `42` | `SmallInteger` / `LargeInteger` | `ProtoInteger` (tagged 54-bit or heap) |
| `3.14` | `Float` | `ProtoDouble` |
| `'hola'` | `String` | `ProtoString` (inline ≤6 bytes UTF-8, else heap rope) |
| `#foo` | `Symbol` | Strong symbol via `createSymbol` |
| `#(1 2 3)` | `Array` (frozen) | `ProtoTuple` |
| `{ a. b. c }` | `Array` (dynamic) | `ProtoList` |
| `true` / `false` | `True` / `False` singletons | `ProtoSpace` constants |
| `nil` | `UndefinedObject` singleton | `PROTO_NONE` |
| `[:x \| x + 1]` | `BlockClosure` | `ProtoObject` with captured closure |

### 5.4 Built-in class hierarchy (bootstrapped at runtime init)

```
Object
  ├── Number
  │     ├── Integer ──┬── SmallInteger
  │     │             └── LargeInteger
  │     └── Float
  ├── Collection
  │     ├── String
  │     ├── Symbol
  │     ├── Array
  │     ├── OrderedCollection
  │     ├── Dictionary
  │     └── Set
  ├── BlockClosure
  ├── Boolean ── True / False (singletons)
  ├── UndefinedObject (singleton nil)
  ├── Exception ── Error ── (and user subclasses)
  ├── Future
  └── Actor
```

The hierarchy is bootstrapped in C++ at `STRuntime` construction (`Bootstrap.cpp`). Methods on built-in classes are written in `.st` files (`lib/core.st`, `lib/collections.st`, etc.) that the runtime loads on startup, except for selectors that must be primitive (`<primitive: #...>`).

### 5.5 Blocks and closures

Each block is compiled as its own `ProtoBytecodeModule` with its own constant pool. The closure analysis pass classifies free variables:

- **Read-only over the home frame's locals** → captured as an immutable snapshot (a `ProtoSparseList` reference).
- **Read/write over the home frame's locals** → captured as a mutable reference to the home frame's `closureLocals`.

A `BlockClosure` is a `ProtoObject` with three attributes:

- `__bytecode__` → pointer to the block's bytecode module.
- `__captured__` → snapshot or mutable reference, per the above.
- `__home_context__` → the originating `ProtoContext`, needed for non-local `^` return.

Non-local return (`^` from inside a block whose home frame has already returned) raises `BlockContext>>cannotReturn:` as Smalltalk-80 specifies.

### 5.6 `self`, `super`, and `self asActor`

- `self` always refers to the **wrapped object** inside an actor method, never to the proxy. Self-sends are normal synchronous dispatch.
- `super` searches starting from the first parent prototype of the method's defining class, not from the receiver's prototype.
- `self asActor`: when executed inside a method running on the actor scheduler, returns the currently-dispatching `Actor` proxy. Outside that context, it creates a fresh actor wrapping `self`. This lets methods schedule async self-callbacks (timers, recurring ticks) without changing the meaning of plain `self`.

## 6. Actor model

### 6.1 Actor (class)

An `Actor` is a `ProtoObject` whose `classPrototype` is the singleton `actorProto`. Attributes:

- `__wrapped__` — the wrapped `ProtoObject`.
- `__mailbox__` — a `ProtoList` used as a Lisp-style cons stack; messages are pushed on the head with CAS, and the worker reverses for FIFO when consuming a batch (Erlang pattern).
- `__state__` — `SmallInteger`: `0=idle`, `1=scheduled`, `2=running`, `3=suspended`.
- `__suspended_frame__` — when suspended, three pointers (PC, operand stack, locals) captured from `ProtoContext`.

`Object>>asActor` is a primitive: it allocates an `Actor` from `actorProto`, sets `__wrapped__`, and returns the proxy.

#### 6.1.1 The actor is the synchronization boundary

A core property of the model — explicit because it has direct consequences for the programmer and for performance — is that **the lock-equivalent is owned by the actor proxy, not by the wrapped object**.

What this means concretely:

- The mailbox + state machine of the `Actor` is what serialises method invocations. The `__state__` transitions (`idle → scheduled → running → suspended → ...`) under CAS are what guarantee "one method at a time on this actor".
- The wrapped object itself is **not synchronised by the runtime**. It has no implicit lock. Its instance variables are plain `ProtoObject` storage and can be read or written without atomics from the actor's running method, because the actor is the only path that reaches them — by convention.
- This is a feature, not an oversight: it means the user-written object can be implemented in idiomatic Smalltalk with mutable instance variables and no concurrency annotations. All concurrency reasoning happens at the `asActor` boundary.

Three consequences the user is responsible for honouring:

1. **One actor per wrapped object** (in practice). Wrapping the same object in two distinct actor proxies and sending messages to both in parallel re-introduces unsynchronised access to the wrapped object's instance variables. The runtime does not detect this — it is the programmer's discipline, exactly as Smalltalk-80 trusts the programmer with `become:` or self-modifying classes.
2. **References to the wrapped object that pre-date `asActor` bypass the lock.** If code holds `obj` before doing `actor := obj asActor` and continues to send `obj someMessage` afterwards, those direct sends run on the caller's thread with no synchronisation against the actor's worker. This is a sharp edge; the spec documents it but does not prohibit it (the same way Smalltalk does not prohibit reading an instance variable via `instVarAt:`).
3. **An actor never reaches inside another actor's wrapped object directly.** Cross-actor communication is exclusively by message send; `actorA actorB someState` is fine because it goes through the proxy. `actorA (actorB __wrapped__) someState` would not be — but it requires intentional misuse to write.

The wrapped object thus stays cheap: no atomic instance variables, no per-field locks, no copy-on-write defensive snapshots. The whole synchronisation budget is one cache line in the `Actor` object, exactly where the model demands it.

### 6.2 Sending a message to an actor

The fast path lives in the `SEND` opcode (§ 5.2). When the receiver's `classPrototype` is `actorProto`, control transfers to `ActorRuntime::enqueue(actor, selector, args)`:

1. Build a `Message` record `{selector, args}` (a small `ProtoObject`).
2. Allocate a `Future` in state `pending`, pin in the runtime's `"protoST-async"` `ProtoRootSet`.
3. CAS-prepend the `{Message, Future}` pair onto `actor.__mailbox__`.
4. CAS-transition `actor.__state__` from `idle` to `scheduled`. If the CAS succeeds, push `actor` onto the scheduler's ready queue.
5. Return the `Future` to the caller's operand stack.

### 6.3 Scheduler

A C++ struct (not a `ProtoObject`) owned by `STRuntime`. Components:

- `workers` — a vector of `ProtoThread` instances, sized to `std::thread::hardware_concurrency()` by default (configurable). Each worker runs a loop: take an actor from the ready queue, drain a batch of messages from its mailbox, run them, requeue if any remained.
- `readyQueue` — Michael-Scott MPMC lock-free queue of `Actor*` pointers. Not exposed to user code, lives entirely in C++.

Per-message worker loop:

```
worker_loop():
  while running:
    actor = readyQueue.take()              // blocks if empty
    actor.state = running
    msg = actor.mailbox.pop()              // FIFO via the reversed batch
    try:
        result = ExecutionEngine.invoke(
            method=lookup(actor.wrapped, msg.selector),
            recv=actor.wrapped,
            args=msg.args,
            running_actor=actor)
        if not suspended_during_invoke:
            msg.future.resolve(result)
    except SmalltalkException as e:
        msg.future.rejectWith(e)
    if not suspended_during_invoke:
        actor.state = idle
        if actor.mailbox.nonEmpty():
            actor.state = scheduled
            readyQueue.push(actor)
```

### 6.4 Future (class)

A `Future` is a `ProtoObject` with attributes:

- `__state__` — `SmallInteger`: `0=pending`, `1=resolved`, `2=rejected`.
- `__value__` — the resolved value (only valid when state is resolved).
- `__error__` — the rejection cause (only valid when state is rejected).
- `__waiters__` — `ProtoList` of suspended actors waiting on this future.
- `__callbacks__` — `ProtoList` of `{kind, block}` pairs registered with `thenDo:` / `catch:`.

Pinned in `"protoST-async"` `ProtoRootSet` while `pending`. Unpinned on resolve or reject.

Primitives:

| Selector | Primitive | Behaviour |
|---|---|---|
| `Future>>wait` | `#FutureWait` | If resolved, push value. If rejected, raise error. If pending, suspend current actor and yield worker. |
| `Future>>thenDo:` | `#FutureThenDo` | Register callback; if already resolved, schedule it immediately. |
| `Future>>catch:` | `#FutureCatch` | Register callback for the rejected case; if already rejected, schedule it immediately. |
| `Future>>resolve:` | `#FutureResolve` | CAS state pending → resolved; wake all waiters; fire all `thenDo:` callbacks. |
| `Future>>rejectWith:` | `#FutureReject` | CAS state pending → rejected; wake all waiters (their `wait` re-raises); fire `catch:` callbacks. |
| `Future class>>whenAny:` | `#FutureWhenAny` | Returns a new future that resolves with the first input that resolves. |

Composition operators are defined in pure Smalltalk in `lib/concurrency.st`:

```smalltalk
Future >> & otherFuture
  ^ Future newFromBlock: [ { self wait. otherFuture wait } ].

Future >> | otherFuture
  ^ Future whenAny: { self. otherFuture }.

Future class >> whenAll: aCollection
  ^ Future newFromBlock: [ aCollection collect: [:f | f wait] ].
```

`Future class>>newFromBlock:` spawns a transient anonymous actor whose only job is to evaluate the block and resolve the resulting future.

### 6.5 Cooperative suspension (the heart of the model)

The `<primitive: #FutureWait>` primitive runs on the worker thread that is currently executing a bytecode method on behalf of some actor. Pseudocode:

```
prim_FutureWait(receiver, args, context):
  future = receiver
  if future.state == 1:                       // resolved
      return future.value
  if future.state == 2:                       // rejected
      raise future.error
  // pending — suspend current actor
  actor = context.currentActor()              // null if not running inside an actor
  if actor is null:
      // calling wait outside an actor (e.g. main thread, REPL):
      // block the calling OS thread on a condition variable
      return future.blockingWait()
  actor.suspendedFrame = context.snapshot()   // 3 pointers
  future.waiters.add(actor)
  actor.state = suspended
  context.signalActorYield()                  // returns control to worker loop
```

On `Future>>resolve:`:

```
prim_FutureResolve(receiver, args, context):
  future = receiver
  newValue = args[0]
  if not CAS(future.state, pending, resolved):
      return                                  // already settled, no-op
  future.value = newValue
  for actor in future.waiters:
      actor.state = scheduled
      scheduler.readyQueue.push(actor)        // resumes on some worker
  for cb in future.callbacks where cb.kind == #then:
      scheduler.scheduleCallback(cb.block, newValue)
  rootSet.unpin(future)
```

The captured `suspendedFrame` is restored when a worker dequeues the actor:

```
worker_loop continuation when actor.state was suspended on dequeue:
  context.restore(actor.suspendedFrame)
  push resolved value or raise rejection cause onto operand stack
  resume bytecode interpretation from saved PC
```

### 6.6 Cooperative-yield limitations (documented)

- `wait` only yields when invoked from **Smalltalk bytecode**. A C++ primitive that internally calls `Future::blockingWait()` ties up its worker — there is no magic to capture C stack frames. Long-running primitives must be written async (return a Future) rather than blocking.
- `wait` outside an actor context (REPL top level, `main:` of a script) blocks the calling OS thread on a condition variable. This is intentional: the REPL or script's main thread acts like a synchronous client of the actor world.

### 6.7 Error model summary

Exception raised inside an actor method:

1. The bytecode interpreter propagates it up the frame chain.
2. The active `on:do:` handler, if any, catches it (normal Smalltalk semantics).
3. If unhandled, the worker loop catches it at the top of `ExecutionEngine.invoke`, calls `msg.future.rejectWith(exception)`, and moves on. The actor stays alive and processes the next message.

Partial mutations performed before the raise are **not rolled back** — Smalltalk has no transactional default and reintroducing one here would be a heavyweight addition the user explicitly rejected during brainstorming.

### 6.8 Actor-based finite state machines: the digital-twin use case

A digital twin is, in software terms, a long-lived stateful entity that mirrors a real-world artefact and reacts to events affecting it. The conventional way of writing one is a finite state machine: a dispatcher routes each incoming event to the handler corresponding to the current state.

```pseudo
on_event(twin, event):
  match twin.state:
    OPERATING  : OPERATING_handlers[event](twin, event)
    WARNING    : WARNING_handlers  [event](twin, event)
    SHUTDOWN   : SHUTDOWN_handlers [event](twin, event)
```

The actor model expresses the same semantics in **inverted order**. Each method is by itself an event handler; the actor's state is consulted *inside* the method:

```smalltalk
"-- pump.st: a digital-twin example --"
Object subclass: #Pump
  instanceVariableNames: 'state pressure temperature serial'.

Pump >> initialize
  state := #operating.

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
  "..."
  (aReading pressure > 95) ifTrue: [ state := #shutdown. self emergencyStop ].
```

The two formulations are **semantically equivalent**; the order is just transposed. But the actor formulation buys three properties that pure FSM dispatch tables do not give for free:

1. **Atomicity per event.** The actor's single-method invariant (only one method runs at a time) guarantees that `sensorReading:` cannot interleave with `manualCommand:` on the same pump. State transitions are always observed in a consistent state — the same property that motivates writing FSMs at all.
2. **Composition by message passing.** Twins interact by sending messages to one another. Two pumps coordinating, a pump reporting to a supervisor, a supervisor querying a fleet — every interaction is a `send → Future`, and `wait`/`thenDo:`/`whenAll:` compose them. There is no shared state to lock.
3. **Concurrency by default.** Ten thousand pumps are ten thousand actors. The cooperative scheduler (§ 6.3) runs them on a worker pool sized to the host's cores; suspensions on `wait` free workers to serve other twins. No code change is needed to go from 10 twins to 10 000 — the same source scales naturally.

A more idiomatic refactor uses the **state pattern** (each state is an object, the pump delegates to it). protoST supports this with no extra machinery: changing state is one assignment.

```smalltalk
Pump >> sensorReading: aReading
  ^ state pump: self readingReceived: aReading.

OperatingState >> pump: aPump readingReceived: aReading
  aPump setPressure: aReading pressure; setTemperature: aReading temperature.
  ((aReading pressure > 80) or: [ aReading temperature > 90 ])
    ifTrue: [ aPump transitionTo: WarningState new ].

WarningState >> pump: aPump readingReceived: aReading
  (aReading pressure > 95) ifTrue: [ aPump transitionTo: ShutdownState new ].
```

Calls like `pumpA sensorReading: r` are asynchronous (`pumpA` is an actor proxy from `asActor`) and return futures, so a sensor pipeline can fan out to thousands of twins concurrently:

```smalltalk
"-- ingest one batch into the fleet --"
ingestBatch: aBatch into: aFleet
  | futures |
  futures := aBatch collect: [:reading |
      (aFleet pumpForSerial: reading pumpId) sensorReading: reading ].
  ^ Future whenAll: futures.   "resolves when every twin has processed its event"
```

This is the structural argument for choosing actors as the foundation of a digital-twin platform: FSMs become natural, atomicity is free, twin-to-twin interaction is just message passing, and the runtime scales the model out without the user writing concurrency code. The triple combination with protoPython (numerical / ML models for the twins' internals) and protoJS (operator dashboards) over a shared `ProtoSpace` closes the technical loop.

## 7. Module system and UMD integration

### 7.1 File-to-module mapping

A `.st` file is a module identified by its path. Module loading executes the file's top-level declarations in order in a fresh `ProtoContext`. The resulting module object is a `ProtoObject` whose attributes are the top-level identifiers defined in the file (class names, primarily). Identifiers beginning with `_` are not exported.

If the file defines a top-level selector `main:`, the CLI invokes it when the file is run as the principal script (analogous to `if __name__ == '__main__':` in Python but driven by selector presence).

### 7.2 `Import from:`

`Import` is a global alias for the `Smalltalk` module-loading entry point:

```smalltalk
Smalltalk >> import: aLogicalPath
  ^ ProtoSpace current
      getImportModule: aLogicalPath
      as: 'exports'.
```

The returned value is the `ProtoObject` produced by whichever UMD provider resolves `aLogicalPath`. Member access uses ordinary `getAttribute` via `doesNotUnderstand:`; no adapter is interposed.

### 7.3 `STModuleProvider`

protoST registers a `ModuleProvider` with `ProviderRegistry` at runtime initialisation. Its `tryLoad(logicalPath, ctx)`:

1. Resolves `logicalPath` to a `.st` file path using the resolution chain (the runtime ships with a sensible default: search `./`, then a configurable `STPATH` env var).
2. If a file exists, parses it, compiles it to bytecode, runs the top-level in a fresh context, and returns the module object.
3. If not found, returns `PROTO_NONE` so the next provider in the chain may handle the request.

The `STModuleProvider` is registered before the `FileSystemProvider` so `.st` files win when present.

### 7.4 Cross-language module use (free outcome of UMD)

Because `Import from:` consults the UMD chain, importing a Python or JavaScript module from Smalltalk works without bridging code:

```smalltalk
math := Import from: 'math'.        "served by protopyc's provider"
pi   := math pi.

fs := Import from: 'fs'.            "served by protojs's provider"
contents := (fs readFile: '/tmp/x' encoding: 'utf-8') wait.
```

The last line depends on a key insight: `Deferred` (protojs) and `Future` (protoST) both respond to `wait` / `thenDo:` / `catch:` on their `ProtoObject` representation. The wait point is polymorphic at the `ProtoObject` level; no JS-to-ST or Python-to-ST conversion happens.

## 8. File syntax

A `.st` file is a sequence of top-level forms. Three forms are recognised:

```smalltalk
"-- 1. Class declaration --"
Object subclass: #Counter
  instanceVariableNames: 'value'
  classVariableNames: ''.

"-- 2. Method definition (file-out style with `>>`) --"
Counter >> initialize
  value := 0.

Counter >> increment
  value := value + 1.

Counter >> value
  ^ value.

Counter class >> startingAt: n
  | c |
  c := self new.
  c setValue: n.
  ^ c.

"-- 3. Top-level expression, executed at module load --"
Transcript show: 'counter module loaded'; cr.
```

Per § 7.1, when the file defines `main:`, the CLI invokes it with the argv tail as a `ProtoList` of `String`s when the file is run as the principal script.

## 9. CLI and REPL

### 9.1 CLI

```
protost script.st [args...]           Run script.st as principal module.
protost -e '<expr>'                   Evaluate expr, print printOn: result.
protost -i                            Start REPL.
protost compile script.st -o out.stbc Compile to standalone bytecode.
protost --dump-ast script.st          (development) print AST.
```

### 9.2 REPL (Python-style)

- Prompt `st> ` with continuation prompt `... `.
- Multiline auto-detect by parser state: open brackets, unclosed keyword selector, missing block close, method definition without body.
- Persistent history at `~/.protost_history` via libedit/readline.
- TAB completion: language keywords, known selectors of the receiver if statically evaluable, classes currently in scope.
- Pretty-printing of each result via `printOn:`. Trailing `;` suppresses output (matlab/jupyter convention).
- Meta-commands prefixed with `:` (loaded from `lib/repl.st`):

| Command | Effect |
|---|---|
| `:load file.st` | Load module and import its bindings into the REPL namespace. |
| `:reload` | Reset namespace and reload everything previously loaded. |
| `:edit Selector` | Open `$EDITOR` with the method/class source. |
| `:time expr` | Time the evaluation. |
| `:doc X` | Show docstring/category of class or method. |
| `:exit` | Exit the REPL. |

## 10. Virtual environments (venv-style)

protoST ships with a built-in mechanism for isolated environments analogous to Python's `venv`. The motivation: when several `.st` projects coexist on a developer machine, they should be able to use different sets of installed modules, different `STPATH` values, different runtime configurations, and different versions of protoST itself, without crosstalk.

### 10.1 Layout of a venv

```
project/
├── .venv/
│   ├── stenv.cfg                # version + flags (analog of pyvenv.cfg)
│   ├── bin/
│   │   ├── protost              # symlink to the runtime selected at venv creation
│   │   └── activate             # shell script that sets env vars
│   ├── lib/
│   │   └── protoST/
│   │       └── modules/         # installed third-party .st modules
│   ├── cache/
│   │   └── bytecode/            # per-source compiled .stbc cache, hashed by source
│   └── config.toml              # runtime config overrides (worker count, log level, …)
├── stproject.toml               # project manifest (optional, declares dependencies)
└── src/
    ├── main.st
    └── ...
```

`stenv.cfg` (plain text, one key per line) contains:

```
home = /usr/local/bin
version = 0.1.0
include-system-modules = false
```

### 10.2 Discovery and activation

The runtime resolves a venv in this precedence order:

1. The `STENV` environment variable (explicit override).
2. A `.venv/` directory found by walking upwards from the current working directory.
3. The user's home venv (`~/.config/protoST/default-venv/`), if any.
4. No venv: system-wide defaults.

When a venv is active, the runtime:

- Sets `STPATH` to include `<venv>/lib/protoST/modules/` ahead of system paths.
- Routes bytecode caching to `<venv>/cache/bytecode/`.
- Reads `<venv>/config.toml` and applies overrides over the defaults.
- Records the active venv in `Smalltalk environment` (a `ProtoObject` exposing `path`, `version`, `installedModules`, `config`).

### 10.3 CLI surface

```
protost venv create [path]            Create a venv at path (default: .venv).
protost venv activate                 Print the shell snippet to source.
protost venv info                     Show the currently active venv.
protost venv install <module>         Install a module into the active venv.
protost venv freeze                   Print installed modules in a reproducible form.
```

The `.venv/bin/activate` script is portable POSIX shell (with a separate `.fish` and `.ps1` variant). It manipulates `PATH`, sets `STENV`, and exposes `deactivate` to unwind.

### 10.4 Project manifest

`stproject.toml` (optional, at project root) declares the runtime version and direct dependencies:

```toml
[project]
name = "myapp"
version = "0.1.0"
requires-st = ">=0.1,<0.2"

[dependencies]
"logging" = "^0.3.1"
"http-client" = "^1.2"
```

`protost venv install` reads this manifest and resolves dependencies. The dependency-resolution algorithm and the package format are intentionally simple in F1: a module is a `.st` file (or a directory with an `index.st`) and a version is a directory suffix. A proper package index is a later phase.

### 10.5 Implementation notes

- The venv layer is **entirely a configuration overlay**: the runtime itself is unchanged. The activation logic lives in `src/runtime/Venv.cpp`, ~200 lines, no protoCore dependency beyond reading environment variables and adjusting `STPATH` before any module load.
- Bytecode cache invalidation: keyed by `(absolute_path, mtime, source_hash, protoST_version)`. A stale entry is recompiled silently.
- The `Smalltalk environment` `ProtoObject` is reachable in user code, so library authors can branch on `installedModules` if they need feature detection without a separate config API.

## 11. Integrated debugger

A debugger is provided **from F2 onward**, not deferred to a later phase. It exists in two forms sharing the same core:

1. A **CLI debugger** (`protost -d script.st` or hitting `Halt now` mid-execution) — interactive, like Python's `pdb`.
2. A **DAP server** (`protost --dap`) — speaks the Debug Adapter Protocol so VS Code, JetBrains, etc. can attach. F2 ships the CLI; the DAP variant is layered on top once stable.

Both share the same `Debugger` core, which is a normal `ProtoObject` in the runtime.

### 11.1 Why the actor model makes the debugger easy

The cooperative suspension mechanism that `Future>>wait` will require (§ 6.5) is **structurally identical to a breakpoint**: snapshot PC + operand stack + locals into an attribute, return control to a higher authority, restore later. The debugger reuses the same primitive shape:

- A breakpoint hit raises a `Halt` exception with the snapshot attached.
- The interpreter's top-level catch — in F2 the main thread of `protost -d`, from F6 the actor worker loop — routes `Halt` to the active debugger session instead of treating it as an unhandled error.
- The debugger inspects/modifies the snapshot.
- On `continue`, the snapshot is restored.

The implementation differs between phases:

- **F2 (single-threaded)**: the debugger session runs on the same thread that was executing user code. The interpreter blocks inside the debugger command loop; when the user types `cont`, the loop returns and the interpreter resumes from the saved PC. This is identical to how `pdb` works in CPython.
- **F6 (actor scheduler)**: only the actor that hit the breakpoint suspends; its worker is freed for other actors. Resumption pushes the suspended actor onto the ready queue. Other actors keep running concurrently — see § 11.6.

No new VM mechanism is needed — only an additional exit reason at the interpreter top level.

### 11.2 Breakpoints

- **Explicit halt in source**: `Halt now`, `Halt now: 'condition'`. Compiles to a primitive `<primitive: #DebuggerHalt>`. Zero overhead when no debugger is attached (the primitive checks the runtime flag `debugger.attached` and returns immediately if false).
- **By location**: `Debugger breakAt: 'Counter>>increment'` registers a watch in the runtime. The compiler emits a check after each prologue if the runtime flag is set; otherwise nothing. (One compilation mode bit; no perf cost when debugging is off.)
- **Conditional**: `Debugger breakAt: 'Counter>>increment' when: [:ctx | (ctx self) value > 100]`. The condition is a `BlockClosure` evaluated at the breakpoint; if it returns `true`, the hit proceeds.

### 11.3 CLI commands (analogous to pdb)

| Command | Action |
|---|---|
| `s` / `step` | Step into the next send. |
| `n` / `next` | Step over (stay at same frame depth). |
| `f` / `finish` | Run until the current frame returns. |
| `c` / `cont` | Continue until next breakpoint or completion. |
| `r` / `return <expr>` | Force-return from the current frame with `<expr>`. |
| `up` / `down` | Move within the suspended stack. |
| `where` / `bt` | Print the suspended stack. |
| `locals` | List local variables of the current frame. |
| `print <expr>` / `p` | Evaluate `<expr>` in the current frame. |
| `eval <expr>` | Like `print` but expression can have side effects, and may be a block. |
| `break <Class>>>selector` | Set a breakpoint. |
| `info breaks` | List breakpoints. |
| `watch <expr>` | Print the expression's value at every stop. |
| `actors` | Show currently scheduled / suspended / running actors. |
| `actor <id>` | Switch debugging focus to actor `<id>`. |
| `quit` | Abandon the run. |

### 11.4 Inspecting state

Each frame exposes a `ProtoObject` view:

```smalltalk
"-- inside the debugger, you have access to: --"
frame := Debugger current.
frame self.            "the receiver"
frame locals.          "Dictionary of local var → value"
frame stack.           "OrderedCollection: current operand stack contents"
frame pc.              "current bytecode offset"
frame method.          "the method object being executed"
frame sender.          "the calling frame"
```

This is the same handle the `<primitive: #DebuggerHalt>` produces, so REPL-style exploration is uniform.

### 11.5 Forwards to DAP

The CLI commands above are a strict subset of DAP semantics; mapping them to a DAP server in a later phase is mechanical:

- `setBreakpoints` ↔ `Debugger>>breakAt:`
- `stackTrace` ↔ `where`
- `variables` ↔ `locals`
- `evaluate` ↔ `print`
- `next` / `stepIn` / `stepOut` / `continue` ↔ identical CLI verbs
- `Continued` / `Stopped` events ↔ raised whenever the worker loop transitions in or out of the debugger session.

The CLI is implemented in `lib/debugger.st` (most of it pure Smalltalk) plus `src/debugger/DebuggerRuntime.{h,cpp}` (≈300 lines C++ for the breakpoint table, the halt primitive, and the DAP-protocol scaffold).

### 11.6 Multi-actor debugging

Because actors are cooperative, when a breakpoint hits on actor A only A is paused; B, C, … keep running on their workers (unless they `wait` on a `Future` owned by A's stalled computation, in which case they suspend naturally). The debugger session shows the universe of actors and lets the user switch focus (`actor <id>`) or set per-actor breakpoints (`Debugger break: ... onActor: 7`). The `actors` command displays state, mailbox depth, last selector executed, and time spent. This is one of the strongest demos of the cooperative scheduler.

## 12. Repository layout

```
protoST/
├── CMakeLists.txt
├── CLAUDE.md
├── DESIGN.md                                   ← root-level pointer; the canonical spec lives at docs/superpowers/specs/2026-05-19-protost-design.md
├── README.md
├── CHANGELOG.md
├── LICENSE
├── include/protoST/
│   ├── STRuntime.h
│   ├── STValue.h
│   └── primitives.h
├── src/
│   ├── frontend/
│   │   ├── Lexer.{h,cpp}
│   │   ├── Parser.{h,cpp}
│   │   ├── AST.h
│   │   └── Compiler.{h,cpp}
│   ├── runtime/
│   │   ├── ExecutionEngine.{h,cpp}
│   │   ├── Opcodes.h
│   │   ├── STRuntime.cpp
│   │   ├── Bootstrap.cpp
│   │   └── Venv.{h,cpp}              # venv discovery + activation
│   ├── actor/
│   │   ├── ActorRuntime.{h,cpp}
│   │   ├── Scheduler.{h,cpp}
│   │   └── Future.{h,cpp}
│   ├── debugger/
│   │   ├── DebuggerRuntime.{h,cpp}   # halt primitive + breakpoint table
│   │   ├── BreakpointTable.{h,cpp}
│   │   └── DapServer.{h,cpp}         # DAP scaffold (filled in later phase)
│   ├── modules/
│   │   └── STModuleProvider.{h,cpp}
│   ├── primitives/
│   │   ├── int_prims.cpp
│   │   ├── str_prims.cpp
│   │   ├── list_prims.cpp
│   │   ├── block_prims.cpp
│   │   ├── future_prims.cpp
│   │   └── debugger_prims.cpp        # #DebuggerHalt, #DebuggerEval, etc.
│   ├── venv_template/                # files copied by `protost venv create`
│   │   ├── stenv.cfg.in
│   │   ├── activate
│   │   ├── activate.fish
│   │   └── activate.ps1
│   └── main.cpp
├── lib/
│   ├── core.st
│   ├── collections.st
│   ├── exceptions.st
│   ├── streams.st
│   ├── concurrency.st
│   ├── debugger.st                   # CLI commands and frame helpers
│   └── repl.st
├── tests/
│   ├── unit/
│   ├── lang/
│   └── actor/
└── examples/
    ├── hello.st
    ├── counter_actor.st
    ├── pipeline.st
    └── ping_pong.st
```

## 13. Implementation phases

Each phase delivers a runnable end-to-end artifact.

| Phase | Scope | "Done" criterion |
|---|---|---|
| F1 — Skeleton + parser + venv scaffold | Lexer, parser, AST printer. `protost --dump-ast`. `protost venv create/activate/info` working (the runtime command exists and creates the directory layout even though there is nothing yet to install into it). | Parser accepts the full ST-80 minimal subset plus module declarations. A fresh `protost venv create .venv && source .venv/bin/activate && protost venv info` shows the venv as active. |
| F2 — Compiler + bytecode + synchronous interpreter + CLI debugger | AST → bytecode. ExecutionEngine. `SmallInteger`, `Boolean`, `String`, `BlockClosure` operating. `Halt now` + `DebuggerRuntime` + CLI commands (step / next / continue / locals / print / where). No actors, no modules. | `protost -e '(1 to: 100) inject: 0 into: [:a :b \| a + b]'` returns 5050. `protost -d hello.st` lets the user step through a method, inspect locals, and continue. |
| F3 — Complete object model | `subclass:`, `>>`, inheritance, `self`/`super`, instance creation. Collections in `lib/`. Breakpoints by location (`Debugger breakAt: ...`). | Language test suite (without actors) green. Debugger can break inside any user method. |
| F4 — Exceptions | `Error signal:`, `on:do:`, non-local return from blocks. Debugger pauses on uncaught exceptions. | Exception test suite green. Debugger demo: run a failing test, drop into the debugger at the raise site, inspect the frame. |
| F5 — Modules + UMD provider | `Import from:`, `STModuleProvider` registered, `main:` invocation. Venv-aware module resolution (the active venv's `lib/protoST/modules/` is consulted first, then system). `protost venv install <module>` works for `.st` modules. | Multi-file programs work. Importing `.py` or `.js` from `.st` smokes. Two different venvs with different versions of the same module coexist. |
| F6 — Actors + Future | `asActor`, mailbox, scheduler, `Future>>wait` with cooperative yield, `thenDo:` / `catch:`, `&` / `\|`, `whenAll:` / `whenAny:`. Debugger gains `actors` and `actor <id>` commands. | Actor test suite green, including a 10k-actor ping-pong stress test. `actors` lists alive actors and lets the user attach to a specific one. |
| F7 — REPL | `protost -i` with multiline, history, completion, meta-commands. REPL respects the active venv. | Interactively usable session. Inside an activated venv, `:load` resolves from the venv first. |
| F8 — DAP server | `protost --dap` exposes the existing debugger over the Debug Adapter Protocol. VS Code extension consumes it. | Setting a breakpoint, hitting it, inspecting variables, and continuing all work from VS Code. |
| F9 — Tuning | Profile, identify hot paths, ensure protoCore's caches are being hit, adjust worker pool defaults, benchmark vs protoJS / protoPython. | Standard benchmarks (Richards, mandelbrot, parallel ping-pong) at ≥ 50 % of protoPython's throughput. |

## 14. Performance discipline (cross-cutting)

- Operand stack and locals: never `std::vector`; always `ProtoList` / `ProtoSparseList`. (Rule absolute.)
- Selectors: perpetual via `createSymbol`; bytecode references them by index into the module's constant pool. (Mechanism A.)
- Pending Futures and registered callbacks: pinned in `STRuntime::asyncRootSet`. (Mechanism B.)
- Numeric primitives: dispatch from `<primitive: #IntAdd>` straight to `ProtoIntegerImplementation::implAdd`. No interpreter loop, no allocations beyond the result Cell.
- Inline caching of selector dispatch: **inherited from protoCore via `ProtoContext`** (cf. `feedback_protopython_attribute_delegation`, `project_protocore_main_thread_detection`). protoST emits `SEND` and benefits without adding a layer.
- Ready queue and worker pool: MPMC lock-free C++, no `ProtoObject` allocation in the dispatch hot path.
- Actor suspension cost: three attribute writes (PC, operand stack, locals) on the actor; resumption is the symmetric read. Zero stack copy.

## 15. References

- `protoCore/DESIGN.md` — memory model, GC bridging mechanisms, prototype object model, ProtoRootSet.
- `protoCore/docs/USER_GUIDE_UMD_MODULES.md` — UMD ModuleProvider interface.
- `protoCore/docs/MODULE_DISCOVERY.md` — UMD resolution chain.
- `protoJS/ARCHITECTURE.md` — analogous runtime, especially `Deferred`, `CPUThreadPool`, the "no std::vector for execution state" rule, and the bytecode interpreter pattern over `ProtoContext`.
- `protoPython/DESIGN.md` — analogous runtime; bytecode format; HPy/native-extension bridging policy.
- `protoCore/docs/conceptual_introduction.rst` — prototype object model rationale, immutability rationale.

## 16. Open items deliberately left for the implementation plan

- Exact grammar of the parser (especially handling of nested keyword sends and cascade precedence). Will be fixed in F1's grammar document.
- Exact opcode set and numeric encoding (the 2-byte format leaves 256 opcodes; the actual set is finalised in F2).
- Mailbox eviction policy if an actor is GC-unreachable from anywhere except its own mailbox (cycle). Likely answer: actors are pinned in a per-runtime root list and explicitly stopped; not addressed in F6 but flagged.
- Configuration interface for worker pool size, scheduler quanta, mailbox cap. Likely environment variables + a `STRuntime::configure(...)` API; finalised in F9.
- Performance tuning policy for protoCore's caches when many short-lived actors come and go quickly — pending observation.
