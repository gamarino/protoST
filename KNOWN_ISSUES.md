# protoST — Known Issues

This file records the hard edges of the runtime that are **not** language
design choices, with their bounds, so you know when each one applies. It was
first written for the 0.1.0 release and is kept current.

For *language-level* gaps — features not yet implemented, intentional
departures from Smalltalk-80, and open bugs — see
[`docs/STATUS.md`](docs/STATUS.md).

---

## K1 — One `STRuntime` per process (open)

**What.** Constructing more than one `STRuntime` in a single OS process is not
supported. The `protost` CLI always constructs exactly one, so this does not
affect normal use.

**Why.** Some process-global state — protoCore's UMD module cache, keyed by
logical path — is not isolated per `ProtoSpace`. A second runtime can
mis-resolve `Import from:` against the first runtime's (already freed) space.
(The protoST module provider itself resolves its runtime per `ProtoSpace`
since S6.)

**Bounds.** Affects only an embedder that builds multiple runtimes in one
process. The larger half of this hazard — function-local `static` caches
holding per-`ProtoSpace` interned symbols — was fixed in 0.1.0. The residual
is the module-provider / cache layer. The unit-test binary builds many
runtimes and is therefore run one test per process by `ctest`.

**Status.** Open — to be closed by making the UMD module provider and cache
per-runtime. See deviation D2 in `docs/STATUS.md`.

## K3 — No `%` string formatting (open)

**What.** There is no printf-style `%` formatting.

**Bounds.** Narrow. Pharo's `format:` is available (`'{1} of {2}' format:
#(3 10)`), as are `,`, `printString`, `printString:`, `printPaddedWith:to:`
and `String streamContents:`.

**Status.** Open; not planned.

## K4 — Two runtimes in one process can share objects but not call each other (partly fixed in protoCore 2.6.0)

**What.** A protoScala program in the same process can import a protoST module
and receives the very same object (no copy, and it survives collections in
both spaces). With protoCore 2.5.0 it could not read that object's attributes;
with protoCore 2.6.0 or later it can, but it still cannot send it messages,
and importing a protoScala module from protoST fails. See
[`docs/INTEROP.md`](docs/INTEROP.md) §0 for what is verified and how.

**Why.** Each runtime owns a `ProtoSpace`. Up to protoCore 2.5.0 a mutable
object's current state was resolved through the space of the context doing
the read, so an object read through the other runtime's context resolved in
the wrong table (observed: wrong values, not an error).

**Status.** The state half is fixed in protoCore 2.6.0 (released 2026-09-29):
the table of mutable states is process-global and every space's collector
marks it (protoCore `docs/GLOBAL_MUTABLE_TABLE.md`). protoScala reads a
protoST object's state and writes to it through its own context (protoScala
test `ProtoSTInterop.AForeignObjectsStateIsReadThroughTheCallersContext`).
protoST 0.5.0 requires protoCore 2.6.1 and 0.6.0 requires 2.11.0, so
both include it; the 0.4.0 package depended on protoCore 2.5.0, where the
defect remains. Still open: a foreign runtime cannot call a protoST method
(INTEROP §4.2), and a protoST cell another runtime keeps in its own
structures must stay reachable in protoST's space.

## K5 — Very large collections need a larger heap limit (open)

**What.** By default the heap holds 10M cells (640 MB). Building a collection
of about 500,000 elements or more needs more (measured on 0.4.0:
`(1 to: 450000) asArray` succeeds, `(1 to: 480000) asArray` and
`(1 to: 500000) asArray` run out of memory),
because protoCore builds a large list with n log n cells; the program then
stops with an out-of-memory message that names the current limit and the
setting to raise it (`PROTOCORE_HEAP_LIMIT_CELLS`, 64 bytes per cell).

**Why the default stays small.** Under a fixed limit protoCore's collector
starts a cycle only as the heap approaches the ceiling (since protoCore
2.12.0, a paced cycle starts a measured runway before it), so a larger
ceiling means fewer, later collections; a 32M-cell default made allocation-heavy actor code 19%
slower (`saturation_big` on one worker: 3.1 s against 3.7 s, interleaved
runs; commit `8e5274f`).

**Why not protoCore's adaptive heap controller.** Since 2.10.0
protoCore offers an opt-in controller (`ProtoSpace::enableAdaptiveHeap`)
that sizes the heap from the live set. protoST does not enable it and keeps
the fixed limit (decision of 2026-10-04, unchanged with protoCore 2.14.5):
protoST's live set right after start-up, about 1.7 M cells, is just below
the controller's initial soft limit of 2 M cells, so a short program pays a
first collection that finds almost no garbage and a collection waited for
at exit. `fib.st` ran about three times slower
under the controller than under the fixed limit, also after protoCore 2.14.2
raised the controller's floor (protoCore
`docs/reports/2026-10-04-final-remeasurement.md` and
`docs/reports/2026-10-04-collector-adjustments.md`, A3). To be revisited,
for example with an initial soft limit above the start-up live set. The
fixed limit is set in `STRuntime::configureHeap`
(`src/runtime/STRuntime.cpp`).

**Status.** Open; a bulk list builder in protoCore would remove the n log n
factor.

---

## Resolved

### K2 — Very large strings / ropes (resolved)

At 0.1.0, a very large protoCore rope (`ProtoString`) — on the order of ~1M
nodes, typically built by a long chain of `,` concatenations — could trigger a
garbage-collector segfault or a "Non-tuple object in tuple node slot" error,
and protoCore's two stress tests for it were disabled.

Fixed in protoCore by the concurrent-mark snapshot (`335ef608`, 2026-05-30)
and the chunked-freelist sweep series (`c3dac9de..11e287a1`); the stress tests
were re-enabled in `0ee42acf` (2026-06-13). Those tests,
`SwarmTest.OneMillionConcats` and `SwarmTest.LargeRopeIndexAccess`, are in
protoCore's
[`test/SwarmTests.cpp`](https://github.com/numaes/protoCore/blob/master/test/SwarmTests.cpp).
Build protoST against a protoCore checkout that includes those commits.
