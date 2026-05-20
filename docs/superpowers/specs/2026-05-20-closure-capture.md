# Closure Capture in Methods — Design Spec

A block inside a *method* must see the method's arguments, local variables,
`self`, and instance variables. Today it sees none of them (they read `nil`):
top-level module closures work, method-level closures do not.

## Root cause (confirmed)

protoST's closure mechanism is a **captured dict** — a mutable ProtoObject in
frame slot 0. `PUSH_CAPTURED`/`STORE_CAPTURED` read/write it; `PUSH_BLOCK`
stamps the creating frame's captured dict onto the block as `__captured__`.

- The compiler's `analyseClosures` already computes, per scope, the names an
  inner block captures (`capturedByScope`), and identifier resolution already
  emits `PUSH_CAPTURED`/`STORE_CAPTURED` for captured names.
- BUT a **method never creates a captured dict**. Slot 0 of a method frame
  holds the *module-level* captured dict (or none). The method's args/locals
  live only in local slots — invisible to a block, whose `PUSH_CAPTURED`
  looks only in slot 0.
- AND blocks are invoked with `self == PROTO_NONE`, so `self` and
  `PUSH_INSTVAR` inside a block do not work.

protoPython solves the analogous problem by snapshotting the enclosing
activation's free variables into a closure frame at function-build time. We do
the equivalent with protoST's captured-dict pattern.

## Fix — two independent parts

### Part 1 — `self` (and instance variables) in blocks

A block inherits the `self` of the method it was textually created in.

- `PUSH_BLOCK` stamps a new attribute `__block_self__` on the block object =
  the creating frame's current `self` (`getSelf(f)` — frame slot 1). For a
  block created inside another block, `getSelf` already yields the inherited
  self, so this is transitive.
- Block invocation — BOTH the direct `value`/`value:` fast-path in
  `ExecutionEngine` AND `invokeBlock` in `block_prims.cpp` — builds the block
  frame with `self` = the block's `__block_self__` (instead of `PROTO_NONE`).
- Result: `PUSH_SELF` and `PUSH_INSTVAR` inside a block work unchanged — they
  read the frame's self slot, which now carries the inherited self.
- This captures the self *value*, so a block that outlives its home method
  still has the right self. No scope analysis needed — `PUSH_BLOCK` always
  stamps it (one cheap `setAttribute`).

### Part 2 — method arguments and locals captured by inner blocks

A method (and the module, and a block) that has captured names gets a captured
dict; its captured arguments are copied in at entry.

- **One captured dict per method**, flat, shared by every block nested in that
  method (however deep). This mirrors how the module already works (one flat
  dict for all module-level blocks).
- New opcode `MAKE_CAPTURED`: allocate a fresh mutable ProtoObject and store it
  in frame slot 0 (where `getCaptured` reads it).
- The compiler emits a **prologue** for a method whose `capturedByScope` (over
  the method and all its nested scopes) is non-empty:
  1. `MAKE_CAPTURED` — create the method's captured dict.
  2. For each method **argument** that is a captured name:
     `PUSH_LOCAL <argSlot>` then `STORE_CAPTURED <argName>` — copy the incoming
     argument value into the dict.
  Captured method *locals* (temps) need no copy — they are assigned in the
  body, and the body already emits `STORE_CAPTURED` for them.
- The method body already emits `PUSH_CAPTURED`/`STORE_CAPTURED` for captured
  names (existing `isCaptured` logic) — no change there.
- Nested blocks: `PUSH_BLOCK` already stamps `__captured__` = the creating
  frame's captured dict. A block inside a method therefore inherits the
  method's dict. A block does NOT emit `MAKE_CAPTURED` (it reuses the inherited
  dict). If a block has captured **arguments** of its own, it emits the
  copy-in part of the prologue (`PUSH_LOCAL`/`STORE_CAPTURED`) but not
  `MAKE_CAPTURED`.
- The module top-level is unchanged: `runTopLevel` already creates and passes
  the module's captured dict; the module emits no `MAKE_CAPTURED`.

## Out of scope (documented limitations)

- Shadowing of a captured name between a method and a block nested in it
  (two distinct captured variables with the same name in nested scopes) — the
  flat per-method dict cannot distinguish them. Rare; not supported in this
  slice.
- Per-activation independence of a block's *own* captured locals across
  multiple invocations of the same block is the standard captured-dict
  behaviour and not changed here.

## Tests

- A method with a block (run via `value:`) that reads a method **argument** →
  the argument's value, not nil.
- A method with a block that reads a method **local** (`| x |`) → its value.
- A block that reads/writes a captured method local and the method observes
  the mutation after the block runs.
- A block that uses `self` → the method's receiver.
- A block that reads an **instance variable** → its value; a block that writes
  one → the method sees the change.
- The `firstEven:`-shape: a method whose block does `^x` over a method
  argument `x` returns the right value (non-local return + capture together).
- A block run via `invokeBlock` (`ifTrue:`/`whileTrue:`) sees method args,
  locals, self, and instance variables — same as the `value:` path.
- A doubly-nested block (block in block in method) reads a method local.
- Top-level module closures still work (regression).
- An actor handler whose method body uses a block over its own args/self
  (regression of the F6 actor path).
