# Cross-Language UMD Interop — Design Spec

**Track 5** (see `docs/ROADMAP.md`). The "three runtimes, one core" track.

## Goal

A coherent strategy — and the protoST-side infrastructure — for protoST to
**consume objects and modules provided by another protoCore runtime**
(protoJS, protoPython) through the UMD module system, when they share a
`ProtoSpace`. And the reverse: protoST modules consumable by the others.

## What exists today

- F5 v2 already registers protoST as a UMD `ModuleProvider` in protoCore's
  `ProviderRegistry`, with a resolution chain (`provider:st`, …). So protoST
  *publishes* — another runtime can `Import` a protoST module.
- protoST's `Import from: '…'` routes through protoCore's UMD; a resolved
  module is a `ProtoObject`.
- Every protoCore runtime represents objects as `ProtoObject`s sharing the
  same 64-byte cell DNA. A "foreign" object (a Python object, a JS object) IS
  a `ProtoObject`; its methods are attributes; a protoST message send
  (`obj selector`) resolves through the same `getAttribute` walk. So at the
  `ProtoObject` level, consuming a foreign object is *structurally*
  transparent — that is the whole point of the shared kernel.

## Honest scope

The full end-to-end demonstration — protoJS, protoPython, and protoST all
embedded in one process, sharing one `ProtoSpace`, importing each other's
live objects — is a **cross-repository integration project**: it requires a
host that links all three runtimes. That is out of scope for a protoST-only
slice and is recorded as the follow-up.

What IS in scope here, and deliverable from the protoST repo alone:

1. **Verify and harden protoST's *consumer* side.** protoST must be able to
   `Import` a module provided by *any* UMD provider — not just its own
   `provider:st` — and use the resulting objects: send them messages, read
   their attributes, pass them around. This is testable without protoJS /
   protoPython by registering a **stand-in foreign provider** (a small C++
   `ModuleProvider`, registered in `tests`, that hands back a `ProtoObject`
   built to look like an object from another runtime — methods as
   primitive-or-bytecode attributes, some state). Driving protoST against it
   exercises exactly the cross-provider consumption path.
2. **The type-mapping strategy**, documented:
   - Immediates (`SmallInteger`, `Boolean`, `nil`, characters) and `String`
     are shared cell types — a foreign integer *is* a protoST integer; no
     conversion. Document this.
   - Collections have impedance: a Python `list` / a JS `Array` is a foreign
     runtime's wrapper object, not a protoST `Array`. Document how protoST
     code should treat a foreign collection (send it the foreign protocol, or
     convert via a documented adapter) and where conversion is needed.
   - Foreign callables / methods: sending a protoST keyword/unary message to a
     foreign object dispatches by selector through `getAttribute`; document
     selector-naming considerations across languages.
3. **A `docs/INTEROP.md`** capturing the strategy: how protoST consumes
   foreign UMD modules and objects, the type mapping, the impedance points,
   and a precise description of the cross-repo integration that the real
   tri-runtime demo needs (so whoever does that work has the plan).

## Sub-slice

- **T5-a — Consumer-side interop + strategy.** Register a stand-in foreign
  UMD provider in the test harness; verify protoST `Import`s a foreign-
  provided module and uses its objects (message sends, attribute reads,
  passing them through collections and blocks). Confirm immediates pass
  through with no conversion. Write `docs/INTEROP.md`. Tests + docs. One
  slice — Track 5 is foundation + strategy, not the tri-runtime demo.

## Tests

- A stand-in foreign provider (C++ `ModuleProvider`) registered for the test;
  protoST `Import from: '<foreign>'` resolves it; the imported object answers
  protoST message sends and attribute reads.
- A foreign object carried through protoST collections and blocks behaves as
  an ordinary `ProtoObject`.
- Immediates from the foreign side (`42`, `true`, a string) are usable
  directly in protoST arithmetic / comparison with no conversion.
- Regression: protoST's own module system (`provider:st`, `Import from:` of
  a `.st` file, the stdlib) stays green.

## Out of scope (the cross-repo follow-up)

The live tri-runtime process — protoJS + protoPython + protoST sharing a
`ProtoSpace`, importing each other's runtime objects end to end — needs a
host linking all three repos. `docs/INTEROP.md` specifies what that host and
that demo require; building it is a separate, cross-repository effort.
