# Non-Local Return — Design Spec

**Track 1, slice 1** (see `docs/ROADMAP.md`). Prerequisite for exceptions.

## Goal

`^expr` inside a block returns from the block's **home method** — the method
activation in which the block was textually created — not merely from the
block. This is standard Smalltalk semantics and the machinery exceptions will
reuse.

```smalltalk
Foo >> firstEven: aCollection
  aCollection do: [ :x | x isEven ifTrue: [ ^x ] ].
  ^nil
```

Here `^x` must return from `firstEven:`, abandoning the `do:` loop.

## Current state

- The compiler emits `Op::RETURN` for an explicit `^expr` (in a method *or* a
  block) and for a method's implicit trailer; `Op::RETURN_TOP` for a block's
  implicit trailer and the module trailer.
- The engine (`ExecutionEngine::runLoop`) treats `RETURN` and `RETURN_TOP`
  identically: pop one frame, push the result onto the caller frame.
- `Frame` has no method/block discriminator and no link to a home frame.
- Blocks evaluated via the direct `value`/`value:` SEND run as frames in the
  same engine (F6 v3 A2). Blocks evaluated via `ifTrue:`/`whileTrue:`/`thenDo:`
  run in a *nested* `ExecutionEngine` (`invokeBlock`) — so a `^` inside such a
  block must unwind *across* an engine boundary.

## Design

### Frame identity

A process-global `std::atomic<unsigned long> g_nextFrameId` assigns every
frame, in every engine, a unique id. `Frame` gains two fields:

- `frameId` — this frame's unique id.
- `homeFrameId` — the id of the home method activation.

For a **method** or **top-level** frame: `homeFrameId == frameId` (it is its
own home). For a **block** frame: `homeFrameId` is inherited — see below.

Global ids mean a `homeFrameId` identifies a home unambiguously even across
engine boundaries and across yield/resume.

### Home propagation

- `PUSH_BLOCK` stamps the new block object with `__home_frame__` =
  *the creating frame's* `homeFrameId`. A block created inside a method gets
  that method's home; a block created inside another block gets the same home
  the outer block carries (so `^` from any nesting depth targets the method).
- When a block is invoked, the block frame's `homeFrameId` is read from the
  block object's `__home_frame__`.

### RETURN vs RETURN_TOP

- `Op::RETURN_TOP` — **always local**: pop one frame, push result to the
  caller. (Block/module implicit trailers. Falling off the end of a block
  returns from the block.)
- `Op::RETURN` — **home-aware**:
  - If `f.homeFrameId == f.frameId` (a method/top-level frame returning) →
    local: pop one frame, push to caller. Identical to today.
  - Else (a block frame running `^expr`) → **non-local return**:
    - If a frame with `frameId == f.homeFrameId` exists in *this* engine's
      `frames_`: unwind — pop every frame from the top down to and including
      the home frame, then push the result onto the home frame's caller (or
      return it to the C++ caller if the home was the outermost frame).
    - Otherwise the home lives in an outer engine (the block was invoked via
      `invokeBlock`): throw `NonLocalReturn{ homeFrameId, value }`.

### `NonLocalReturn` cross-engine propagation

`NonLocalReturn` is a C++ exception carrying the target `homeFrameId` and the
return value. `runLoop` wraps its dispatch so that a `NonLocalReturn` reaching
the top of an engine is handled thus:

- If the engine's `frames_` contains the home frame → unwind to it locally
  (as above), resume normally with the value pushed onto the home's caller.
- If not → let it propagate. `invokeBlock`'s nested-engine call site does NOT
  swallow it; it bubbles to the parent engine, which repeats the check.

The engine that owns the home frame catches it; intermediate engines re-throw.

### Dead home

If a block outlives its home method (the home already returned — the block
escaped as a closure and is invoked later), no live frame matches
`homeFrameId`, in any engine. The `NonLocalReturn` propagates out of the
outermost engine unhandled. For this slice, that surfaces as a
`std::runtime_error("non-local return: home method has already returned")`
(an actor rejects its Future with it; the REPL/`-e` prints it). When the
exceptions track lands, this becomes a signalable `BlockCannotReturn`.

### Snapshot / restore

`snapshotFrames` / `restoreFrames` must serialize `frameId` and `homeFrameId`
per frame. Global ids stay valid across a yield/resume with no renumbering.

## Out of scope — and a design note for the exceptions track

The full exception protocol (`on:do:`, `signal`, `ensure:`, resumable/retry)
is Track 1, slice 2. This slice only delivers non-local return and the
`NonLocalReturn` unwinding machinery that exceptions will build on.

**Design note for that next slice.** C++ `throw`/`catch` is the right tool for
the *unwinding* phase: it runs RAII destructors (`TransientPin`, the actor-lock
and drain guards) correctly and crosses the nested sub-engines for free. Its
one limitation is that a C++ exception cannot be *resumed* — once thrown, the
stack between `throw` and `catch` is gone, and Smalltalk exceptions are
resumable (`resume:`).

The resolution: do NOT use a C++ throw to *find and run* the handler. Keep a
runtime-maintained stack of active `on:do:` handlers, separate from the C++
stack and from `frames_`. On `signal`, search that stack and run the handler
block **in place**, with the signalling stack still intact:

- `resume: v` → the handler just returns; `signal` returns `v`. No unwinding,
  no C++ exception at all.
- `return: v` / handler falls through → *now* unwind to the `on:do:` frame,
  via a C++ exception (the same machinery as `NonLocalReturn`).
- `retry` → unwind to the `on:do:` and re-run the protected block.
- `pass` / `outer` → keep searching the handler stack outward.

So C++ exceptions carry only the unwinding, never the handler search — that is
how protoST gets both clean RAII unwinding *and* resumable exceptions. `ensure:`
blocks are run by the `frames_` unwinder as it passes their frames.

**Mandatory: UMD / native exceptions.** Modules loaded through UMD — and native
primitives generally — can throw C++ exceptions. Supporting them is not
optional. Every boundary where protoST calls into native or UMD-provided code
must `catch (const std::exception&)` (and `catch (...)`) and translate the C++
exception into a protoST exception that `on:do:` can catch. Such a translated
exception is inherently **non-resumable** (the native stack between its `throw`
and protoST's `catch` is already gone) and is marked as such — a `resume:` on it
is itself an error. This requirement reinforces the choice of C++ `throw`/`catch`
as the substrate: the runtime must interoperate with C++ exceptions at the UMD
boundary no matter what, so using the same mechanism internally is consistent
rather than a second, parallel machinery.

## Tests

- `^` from inside a one-level block returns from the method (the `firstEven:`
  shape) — both for blocks run via direct `value:` and via `do:`/`ifTrue:`.
- `^` from a doubly-nested block targets the method, not the inner block.
- `^` from a block at method top level behaves like a normal method return.
- A `RETURN_TOP` block trailer still returns only from the block.
- `^` inside an actor handler's block returns from the handler (resolves the
  Future with that value).
- Dead-home: a block stored, its home method returned, then invoked with a
  `^` inside → the dead-home error.
- Non-local return across a cooperative yield (block does `^(f wait)` …)
  survives snapshot/restore.
