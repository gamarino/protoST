# Exception Protocol — Design Spec

**Track 1, slice 2** (see `docs/ROADMAP.md`). Builds on non-local return
(slice 1, commit `e2cf424`).

## Goal

A Smalltalk-style exception protocol: a class hierarchy, `signal`, protected
blocks with `on:do:`, `ensure:`/`ifCurtailed:`, and the handler actions
`return:`, `retry`, `resume:`, `pass`. Plus a hard requirement: C++ exceptions
thrown by UMD/native modules must be caught and turned into protoST exceptions.

```smalltalk
[ self risky ]
  on: Error
  do: [ :ex | Transcript show: ex messageText. ex return: nil ].

[ stream next ] on: EndOfStream do: [ :ex | ex resume: nil ].
```

## Why C++ throw/catch is the substrate — but only for unwinding

C++ `throw`/`catch` runs RAII destructors (`TransientPin`, the actor-lock and
drain guards) correctly and crosses the nested sub-engines for free. But a C++
exception cannot be resumed: once thrown, the stack between `throw` and `catch`
is gone. Smalltalk exceptions are resumable.

**The split.** A C++ throw is NOT used to find and run the handler — only to
*unwind once the handler has decided to unwind*. Finding and running the
handler happens with the signalling stack intact, against a runtime-maintained
handler stack.

## Components

### 1. Exception class hierarchy

A small hierarchy, user-subclassable:

- `Exception` — root. **Resumable.**
- `Error` — `Exception` subclass. **Not resumable** (`resume:` on it is itself
  an error).
- `Warning` — `Exception` subclass. **Resumable**; default action is to print
  and resume.

The runtime adds specific subclasses as needed (e.g. a `MessageNotUnknown`,
`ZeroDivide`); users may `subclass:` their own. Each exception instance carries
at least `messageText` and `selector`/`receiver` where meaningful, plus a
`resumable` flag (class-derived, overridable per instance — a translated
native exception is forced non-resumable).

These are ordinary protoST classes (bootstrap-installed prototypes, like
`Object`/`Actor`/`Future`).

### 2. signal

- `Exception signal`, `Exception signal: aString`, `anExceptionInstance signal`.
- `signal` does NOT throw a C++ exception. It:
  1. builds (or takes) the exception instance,
  2. walks the **handler stack** (below) from newest to oldest for the first
     handler whose guard class matches,
  3. runs that handler block **in place**, passing the exception instance,
  4. acts on what the handler did (see §4).
- If no handler matches → the exception's **default action** runs (`Error`:
  abort the current activation with the message — an actor rejects its Future,
  the REPL/`-e` prints it; `Warning`: print and resume; `Exception`: resume
  with nil).

### 3. The handler stack + on:do:

`[ protected ] on: ExceptionClass do: [ :ex | handler ]`:

- A runtime-maintained stack of active handlers, **separate** from the C++
  stack and from `frames_`. Each entry records: the guard class, the handler
  block, and the `frameId` of the `on:do:` activation (so unwinding has a
  target — reuse slice 1's frame ids).
- `on:do:` pushes a handler entry, evaluates `protected`, and pops the entry
  (on every exit path — normal, unwind, yield).
- `on:do:on:do:` (multiple guards) is sugar over the same mechanism.
- While a handler block runs, its own entry (and inner ones) are disabled, so
  a `signal` inside a handler is caught by an *outer* handler, not itself.

### 4. Handler actions

The handler block runs with the signalling stack intact. What it does decides
whether — and how far — to unwind:

- **`resume: v`** → `signal` returns `v`; the protected computation continues
  from the `signal` point. **No unwinding, no C++ exception.** Only valid for a
  resumable exception; otherwise it is an error.
- **`return: v`** (or the handler block simply falls off its end, yielding its
  last value) → the `on:do:` completes with `v`. Unwind: throw a C++
  `UnwindToHandler{ handlerFrameId, value }` — the same machinery as
  `NonLocalReturn` from slice 1 — caught at the `on:do:` activation, which
  returns `v`.
- **`retry`** → unwind to the `on:do:` activation and re-evaluate `protected`
  from scratch.
- **`pass`** (a.k.a. `outer`) → resume searching the handler stack outward from
  the current handler; if none remains, the default action runs.

### 5. ensure: / ifCurtailed:

- `[ block ] ensure: [ cleanup ]` — `cleanup` runs whether `block` completes
  normally or is abandoned by an unwind.
- `[ block ] ifCurtailed: [ cleanup ]` — `cleanup` runs only on an abnormal
  exit (an unwind passing through).
- Implementation: the construct registers the cleanup against its frame. The
  `frames_` unwinder (the code that pops frames for `NonLocalReturn` /
  `UnwindToHandler`) runs the registered cleanup of each frame it pops. Normal
  completion runs `ensure:`'s cleanup inline; `ifCurtailed:`'s only fires from
  the unwinder.

### 6. Mandatory: UMD / native exception translation

Every boundary where protoST calls into native or UMD-provided code — native
primitives, UMD-provided module methods — must:

```cpp
try { /* native / UMD call */ }
catch (const NonLocalReturn&)   { throw; }   // slice 1 — let it propagate
catch (const FutureYield&)      { throw; }   // F6 v3 — let it propagate
catch (const UnwindToHandler&)  { throw; }   // this slice — let it propagate
catch (const std::exception& e) { /* signal a non-resumable Error(e.what()) */ }
catch (...)                     { /* signal a non-resumable Error("native exception") */ }
```

The translated `Error` is **non-resumable** (the native stack is already gone)
and goes through the normal §2 `signal` path — so a UMD exception can be caught
by an ordinary `on: Error do:` handler. Audit every native call site: the
primitive dispatch in `ExecutionEngine`, `invokeBlock`, `drainOne`'s method
dispatch, the UMD provider `tryLoad` path, every `prim_*` that calls out.

## Suggested sub-slices

This slice is large; implement and land it in order:

- **EXC-a** — class hierarchy (`Exception`/`Error`/`Warning`), `signal`/
  `signal:`, the handler stack, `on:do:`, and `return:`/fall-through with
  `UnwindToHandler`. Default action for unhandled exceptions. (The catch-and-
  terminate core — already useful on its own.)
- **EXC-b** — `resume:`, `retry`, `pass`/`outer`; `on:do:on:do:`.
- **EXC-c** — `ensure:` / `ifCurtailed:` and the unwinder cleanup hook.
- **EXC-d** — UMD/native C++ exception translation across all native
  boundaries.

## Interaction notes

- `UnwindToHandler` and `NonLocalReturn` share the `frames_` unwinder and the
  cross-engine propagation pattern (`runLoop` catches and checks ownership;
  nested engines re-throw). Keep them as siblings — neither derives from
  `std::exception`, so ordinary `catch (const std::exception&)` ignores them.
- Cooperative yield: a handler stack entry must survive an actor's
  yield/resume — snapshot/restore it alongside `frames_`, keyed by the
  surviving global frame ids.
- The actor lock is held across a handler running in place; a handler must not
  send to its own actor as an agent (same standing limitation as any handler
  code).

## Tests

Per sub-slice: catch-and-return; unhandled `Error` default action; nested
`on:do:`; `resume:` continuing the computation; `retry`; `pass` to an outer
handler; `ensure:` on both normal and abnormal exit; `ifCurtailed:` firing only
on unwind; an exception crossing a cooperative yield; a primitive that throws a
C++ `std::exception` being caught by an `on: Error do:` handler.
