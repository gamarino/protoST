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
memory that is not cells. Up to protoCore 2.14.5 the largest part, when the
heap was big, was the collector's mark work list: a `std::vector` that grew
by doubling and held the old buffer while it copied. Measured on 0.6.3 with
`PROTOCORE_ADAPTIVE_HEAP=0`: `(1 to: 500000) asArray` stopped at the 640 MB
cell limit with a peak RSS of 1.19 GB, the large allocations besides the
cell blocks being that vector's 32 to 512 MB buffers
(`benchmarks/reports/2026-10-12-pre-talk-063.md`, section 3). protoCore
2.14.6 bounds the work list (a fixed 8 MiB stack with overflow rescan): the
same program now peaks at 690 MB instead of 1,165 MB (1,192,800 kB, the
1.19 GB above) and reaches the
out-of-memory exit in 3.0 s instead of 5.9 s
(`benchmarks/reports/2026-10-12-protocore-2.14.6.md`, 5 runs each). The
remaining excess over the cells is the collector's marked list (8 bytes per
live cell) and the rest of the process; leave some margin above a cell
limit.

**Status.** The default no longer runs out of memory on collections of this
size; the n log n cost remains (a bulk list builder in protoCore would
remove it), and the fixed limits above keep the old bound.

## K7 — Diagnostic text can be cut at a NUL (open, bounded)

A `String` holds NUL characters everywhere data travels (file contents, text
read from a stream, `utf8Decoded`, symbols, error messages built from user
text). The remaining places that report a problem by throwing
`std::runtime_error` (about 270 in `src/`, such as "expects a String
argument") hand the text to Smalltalk through `what()`, a `const char*`, so a
NUL inside user text embedded in such a message shortens the **message**. It
never shortens data: no data path goes through them. The messages of the
runtime's own signalling (`signalErrorOfClass`, `signalNativeError`,
`ClassedErrorSignal`, the engine's undefined-global and doesNotUnderstand
texts) are length-aware. Another bound is protoCore's error factory, which takes a
`const char*`, so a message created there ends at a NUL. Converting the
remaining throws is a mechanical refactor with a review of its own, not done
in 0.9.0.

Two protoCore facts sit under this, to be raised there:
`ProtoString::createSymbol(ctx, const std::string&)` calls `c_str()`
(`ProtoString.cpp`, around line 1307), so a NUL-bearing `std::string` is
truncated silently (protoST uses `ProtoString::intern`, which is
length-aware, for `asSymbol`); and `fromStdString` truncates the same way.

## K8 — `BinaryFile`: what the end guard cannot see (open, by design)

`append:` and `truncateTo:` need the end of the file alone; an extending
`writeAt:bytes:` shares it with other extending writes. protoST enforces this
in one process on one `BinaryFile` object and signals `Error` when two such
calls collide. It cannot detect:

- appenders or writers in **other processes**, or through **another
  `BinaryFile` object** opened on the same file (take `lock` around them);
- a `writeAt:bytes:` running while `truncateTo:` shrinks the file: the write
  may extend the file without the guard and overwrite bytes a later `append:`
  puts there. The offsets `append:` answers stay correct. This race is the
  caller's, as any two writes over the same bytes are.

The lock is advisory and belongs to the open file, not the process. On
Windows the locked byte cannot be read or written through another descriptor:
lock a dedicated file. An unclosed `BinaryFile` keeps its descriptor and lock
until the program ends.

**Follow-up in protoIO (0.3.1).** The guard lives in protoST. A runtime that
calls protoIO's `appendData` directly has none, and the same guard could move
into protoIO for every runtime.

## K9 — Timers: one thread per runtime, and exit can wait (open, by design)

The timer service is one ProtoThread per `STRuntime` (the design said one per
process; a process normally has one runtime, and a guest runtime in a host's
space has its own). A deadline is only a promise that the settle will be
queued: a worker must be free to run it. At exit, pending timers are dropped,
but a worker blocked in a nested `waitWithin:` (inside a block of an actor
method) is joined only when that wait ends, so `~STRuntime` can be delayed up
to that deadline. The timer thread itself never delays exit. If building or
queueing a due timer fails for lack of memory, the timer is retried every 10
ms and the failure is reported on stderr at most once per second; protoCore
has no allocation-failure hook, so those paths are verified by reading only.

## K10 — What the 0.9.0 tests do not prove (open)

- **No ThreadSanitizer build of protoST exists.** The timer service, the
  actor tests and the `BinaryFile` guards were never run under TSan; their
  evidence is the repeated runs (20 of 20 for the timer cases), the mutation
  checks recorded in the task reports and the review of the lock-free code.
- **`InternalError` cannot be forced.** protoIO raises it only for OpenSSL
  failures that are practically unreachable, and a test-only primitive was not
  wanted. The mapping is one switch (`src/primitives/ProtoioErrors.h`),
  covered by the build and by reading.
- **Tests that depend on the platform say so.** `binfile_lock_close.st`
  needs Linux's `/proc/locks` for its handshake; elsewhere it ends with a
  `SKIP:` line and CTest reports it as *Skipped* (`tests/conformance/README.md`,
  "Visible skips"). That the skip, and the test on macOS and Windows, behave as
  intended is confirmed only by those platforms' CI runs.
  `cli_binary_lock_protobase` and `cli_binary_interop` are skipped, visibly,
  without `python3` (and the `msgpack` module, for the second).
- **`BinaryFile` and ProtoBase files.** The lock is verified against a Python
  peer that takes ProtoBase's `OwnerLock` the same way, and MessagePack,
  `struct`, CRC-32 and BLAKE2b bytes against CPython; no ProtoBase storage
  file has been read or written by protoST.

## K11 — protoCore has no structural list split (open, in protoCore)

`ProtoList::getSlice` and `splitFirst` rebuild the list by appending, so
taking the tail of a large list costs O(size). The MessagePack decoder avoids
it where it can (it removes from the end of its work list, and slices once when
the children are fewer than the stack), but code that wants a constant-time
split cannot have it until protoCore offers one.

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
