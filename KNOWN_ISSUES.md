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

## K4 — Two runtimes in one process cannot use each other's objects yet (open)

**What.** A protoScala program in the same process can import a protoST module
and receives the very same object (no copy, and it survives collections in
both spaces), but it cannot read that object's attributes or send it
messages; importing a protoScala module from protoST fails. See
[`docs/INTEROP.md`](docs/INTEROP.md) §0 for what is verified and how.

**Why.** Each runtime owns a `ProtoSpace`, and protoCore resolves a mutable
object's current state through the space of the context doing the read, so an
object read through the other runtime's context resolves in the wrong table
(observed: wrong values, not an error). This is a protoCore-level constraint
on multi-space processes.

**Status.** Open; needs protoCore support for cross-space mutable-object
access. protoST's own side of the protocol (serving and consuming UMD
modules) is implemented and tested with a simulated provider.

## K5 — Very large collections need a larger heap limit (open)

**What.** By default the heap holds 10M cells (640 MB). Building a collection
of about 500,000 elements or more needs more (measured on 0.4.0:
`(1 to: 450000) asArray` succeeds, `(1 to: 480000) asArray` and
`(1 to: 500000) asArray` run out of memory),
because protoCore builds a large list with n log n cells; the program then
stops with an out-of-memory message that names the current limit and the
setting to raise it (`PROTOCORE_HEAP_LIMIT_CELLS`, 64 bytes per cell).

**Why the default stays small.** protoCore's collector waits for the ceiling
before collecting; a 32M-cell default made allocation-heavy actor code 19%
slower (`saturation_big` on one worker: 3.1 s against 3.7 s, interleaved
runs; commit `8e5274f`).

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
