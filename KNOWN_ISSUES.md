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

## K5 — Very large collections cost n log n cells (partly resolved in 0.6.2)

**What.** protoCore builds a large list with n log n cells, so a collection
of hundreds of thousands of elements needs far more heap than its elements.
Up to 0.6.1 protoST ran under a fixed heap limit of 10M cells (640 MB), and
a collection of about 500,000 elements or more ran out of memory (measured
on 0.4.0: `(1 to: 450000) asArray` succeeds, `(1 to: 500000) asArray` stops
with the out-of-memory message; 0.6.1 still stops at 500,000, 2026-10-12).

**Since 0.6.2** the default memory policy is protoCore's adaptive heap
controller (`STRuntime::configureHeap`, `src/runtime/STRuntime.cpp`), whose
budget is 75 % of physical memory or of the process memory limit (cgroup on
Linux, job object on Windows); a soft limit inside it, 128 MB at start,
triggers the collections and grows only while the program waits for the
collector. Measured on 2026-10-12 (DEV12, 62 GB of RAM): `(1 to: 500000) asArray`
succeeds in 1.7 s with a 0.74 GB peak, `(1 to: 1000000) asArray` in 4.3 s
with 1.7 GB. On the benchmark suite 0.6.2 is faster than 0.6.1 on every
workload but `attr_lookup` (unchanged), by up to 43 % on the actor
workloads, with a fifth to a half of the memory
(`benchmarks/reports/2026-10-12-adaptive-heap-default.md`; that includes the
K6 fix). Peak memory is no longer capped at 640 MB: the controller grows its
soft limit while that shortens the program's waits for the collector (an
allocating 3M-iteration loop peaked at 0.88-3.5 GB in three runs); set
`PROTOCORE_HEAP_LIMIT_CELLS` when a fixed footprint matters.

**Why the fixed limit was kept until 0.6.1, and why that changed.** Under
the controller `fib.st` ran about three times slower than under the fixed
limit (decision of 2026-10-04, protoCore
`docs/reports/2026-10-04-collector-adjustments.md`, A3). The cause was
protoST's, not the controller's (S24 in `docs/STATUS.md`): a recursion that
never loops reached no garbage-collection safepoint, so nothing it allocated
could be reclaimed and every cycle was futile. 0.6.1 made call entry a
safepoint (`benchmarks/reports/2026-10-11-fib-under-adaptive-controller.md`);
0.6.2 then made the controller the default.

**Choosing the policy.** In order of precedence:

| Environment | Policy |
|---|---|
| `PROTOCORE_ADAPTIVE_HEAP=1` | the controller; `PROTOCORE_HEAP_LIMIT_CELLS`, when set, is its budget |
| `PROTOCORE_HEAP_LIMIT_CELLS=<hard>` or `<soft>,<hard>` | a fixed limit of that many cells (64 bytes each); `0` means no limit |
| `PROTOCORE_ADAPTIVE_HEAP=0` | the fixed limit of 0.6.1 and earlier: 10M cells (640 MB), at most a quarter of physical memory |
| none of them | the controller, with its automatic budget (the default) |

A program whose live objects fill the budget or the limit stops with exit
status 3 and a message that names the limit in force and how to raise it.

**The limits count cells, not the process's memory.** protoCore never takes
more cells from the system than the limit allows, but the process also holds
memory that is not cells. The largest part, when the heap is big, is the
collector's mark work list: a `std::vector` in protoCore's collector thread
that grows by doubling and holds the old buffer while it copies. Measured on
0.6.3 with `PROTOCORE_ADAPTIVE_HEAP=0`: `(1 to: 500000) asArray` stops at the
640 MB cell limit with a peak RSS of 1.19 GB, and the large allocations
besides the cell blocks are that vector's 32, 64, 128, 256 and 512 MB
buffers (`benchmarks/reports/2026-10-12-pre-talk-063.md`, section 3). Set a
cell limit with that margin in mind; bounding the work list is a protoCore
matter.

**Status.** The default no longer runs out of memory on collections of this
size; the n log n cost remains (a bulk list builder in protoCore would
remove it), and the fixed limits above keep the old bound.

---

## Resolved

### K6 — Collection pauses grew with the number of threads (resolved in 0.6.2)

Every protoST thread that had run code kept its frames in a
`ProtoContext::automaticLocals` array pre-sized to 1,048,576 slots (8 MB),
and protoCore's stop-the-world root scan visits every slot of every thread's
array, used or not: 4 ms per pause with one thread, 25-31 ms with the twelve
workers of `parallel_speedup`. Under the fixed limit of 0.6.1 the short
benchmarks rarely collected, so the cost stayed hidden; under the adaptive
controller it made `parallel_speedup` with the full pool 14 % slower than on
0.6.1. Since 0.6.2 the array starts at 4,096 slots and doubles when a frame
region needs more, up to the same maximum (`ensureEngineSlots`,
`src/runtime/TransientPin.h`); the longest pause measured on the benchmark
suite is 0.27 ms, and the full pool runs 27 % faster than on 0.6.1
(`benchmarks/reports/2026-10-12-adaptive-heap-default.md`). Test:
`EngineSlots` (`tests/unit/test_heap_policy.cpp`).

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
