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

## K8 — `BinaryFile`: what the end guard cannot see (open, by design)

Every `BinaryFile` routes `append:`, `writeAt:bytes:` and `truncateTo:`
through one `protoio::file::EndGuard` (protoIO 0.4.0). `append:` needs the end
of the file alone; `truncateTo:` needs the whole file alone, in-file writes
included; a `writeAt:bytes:` that extends the file needs the end shared only
with other writes. A call that conflicts with one in progress signals `Error`
naming its selector and does nothing. On one `BinaryFile` object, the bytes of
an `append:` therefore land at the offset it answers, and no write lands past
the end of a file a `truncateTo:` cut meanwhile. Since protoIO 0.4.0 the latter
holds too: before, a `writeAt:bytes:` judged inside the file before a
concurrent `truncateTo:` shrank it could extend the file unguarded. The guard
cannot detect:

- appenders or writers in **other processes**, or through **another
  `BinaryFile` object** opened on the same file (take `lock` around them);
- two writes over the same bytes, which are never ordered (the callers' own
  concern, as in any file API).

The lock is advisory and belongs to the open file, not the process. On
Windows the locked byte cannot be read or written through another descriptor:
lock a dedicated file. An unclosed `BinaryFile` keeps its descriptor and lock
until the program ends.

**Done in protoIO 0.4.0.** The guard moved from protoST into protoIO
(`file::EndGuard` and the guarded `appendData`, `writeAt` and `truncate`), so
every runtime can use it; protoST's own copy is gone.

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

A worker whose nested waits have reached the helping cap (16 buried turns,
`kMaxHelpingDepth` in `src/primitives/future_prims.cpp`) runs no further actor
turn, but it still runs queued timer settles, so that a deadline can end its
wait. A `thenDo:` or `catch:` callback such a settle runs may itself wait,
and that wait helps the same way, so timer callbacks that wait can nest past
the helping cap. That nesting is bounded by the engine nesting limit
(`kMaxNestedEngines`, 1000 levels in `src/runtime/ExecutionEngine.cpp`):
past it the callback fails with the catchable Error "stack depth exceeded
(nested block evaluation)", reported like any callback error; it never
overflows the native stack.

## K10 — What the 0.9.0 tests do not prove (open)

- **ThreadSanitizer: clean against protoCore 3.5.2, but a clean run is not
  proof.** Against a TSan build of protoCore 3.5.2 and protoIO 0.4.0 the
  label ran 580/580 on 2026-11-10 with no report, including the three
  programs below that used to report protoCore's race. History:
  `-DPROTOST_TSAN=ON` and
  the `tsan` label (580 tests: every unit case, the actor, Future, Atom,
  timer and `BinaryFile` programs, the concurrency and debugger CLI tests;
  `docs/INSTALLATION.md`) ran on 2026-11-10 against a TSan build of
  protoCore 3.5.0 and protoIO 0.4.0. Two local runs reported no race in
  protoST; the first CI run found one, in the debugger (two threads resuming
  from a DAP stop wrote one shared field), which is fixed. A clean run is
  evidence about the interleavings it happened to see, not proof. TSan
  reported no race in protoIO. It reported one in protoCore: `ProtoSpace::newThread` called from
  two threads at once reads and writes `ProtoSpace::mainContext` without
  synchronisation (`core/Thread.cpp:494` and `:506`, around
  `core/ProtoContext.cpp:198`), and an interleaving can leave `mainContext`
  on a worker's root context, which dangles once that worker exits. protoST
  starts workers from worker threads (`STRuntime::enterBlockingIO`), so
  programs that block several actors in I/O at once report it
  (`14-io/http_concurrent`, `14-io/net_pool_growth`,
  `15-binary/binfile_lock_actors`; two or three of them per run). protoCore
  3.5.2 fixes it; the CI job that runs the label builds 3.5.2 and gates.
  What stays open is what any TSan run leaves open: it sees only the
  interleavings that happened.
- **`InternalError` cannot be forced from a program.** protoIO raises it only
  for OpenSSL failures that are practically unreachable, and the runtime ships
  no test-only primitive. The mapping (`src/primitives/ProtoioErrors.h`) is
  tested from C++ instead: `tests/unit/test_protoio_errors.cpp` registers a
  primitive in its own runtime that raises a synthesized `protoio::Error` of
  each kind, and a Smalltalk handler checks the class (`InternalError` for
  `Internal`) and the `messageText`.
- **Tests that depend on the platform say so.** `binfile_lock_close.st`
  needs Linux's `/proc/locks` for its handshake; elsewhere it ends with a
  `SKIP:` line and CTest reports it as *Skipped* (`tests/conformance/README.md`,
  "Visible skips"). That the skip, and the test on macOS and Windows, behave as
  intended is confirmed only by those platforms' CI runs.
  `cli_binary_lock_protobase` and `cli_binary_interop` are skipped, visibly,
  without the Python 3 interpreter CMake finds (and its `msgpack` module, for
  the second); CI installs `msgpack`, so there they run.
- **`BinaryFile` and ProtoBase files.** The lock is verified against a Python
  peer that takes ProtoBase's `OwnerLock` the same way, and MessagePack,
  `struct`, CRC-32 and BLAKE2b bytes against CPython; no ProtoBase storage
  file has been read or written by protoST.

## K11 — protoCore had no structural list split (resolved in protoCore 3.5.1)

Through protoCore 3.5.0, `ProtoList::getSlice` and `splitFirst` rebuilt the
list by appending, so taking the tail of a large list cost O(size).
protoCore 3.5.1 slices, splits and concatenates by persistent-AVL join/split
in O(log n), and protoST 0.9.0 is released against 3.5.2. Built from source
against the 3.4.0 floor, the old cost remains; the MessagePack decoder still
removes from the end of its work list and slices once when the children are
fewer than the stack, which is cheap on either.

## K13 — A mailbox batch of more than 2^31 - 1 messages is refused, and lost (open, in protoCore)

An actor's turn takes its mailbox in batches (`ProtoMPSCQueue::takeAll`) and
walks each batch by index. protoCore indexes a list with an `int`
(`getAt`, `getSlice`), so `MailboxCursor::adopt` (`src/runtime/STRuntime.cpp`)
counts a batch through `listIndex` and refuses one of more than `INT_MAX`
messages with an `Error` before it touches the cursor. By then `takeAll` has
already removed those messages from the queue, and nothing in protoST can
reach the elements past `INT_MAX` to park them (`__pending__` is written with
`getSlice`, which is `int`-indexed too), so the batch is lost; the error
leaves the turn (a worker thread reports it on stderr). No protoST program can reach this: such a batch
holds more than 2^31 messages that no turn has consumed, over 128 GB of
message cells. It is recorded because the loss is silent to the senders
(their Futures never settle). A fix needs protoCore to index a list beyond an
`int`, or a `takeAll` that hands over at most a given count.

---

## Resolved

### K12 — A NUL in a string for the OS was refused by protoST only (resolved in protoIO 0.4.0)

The operating system reads a path, an argv element, a shell command line, an
environment name or value and a host name only up to its first NUL, and a
protoST `String` may contain one. Since 0.9.0 protoST refuses such a string
before it reaches the OS, as CPython does with `ValueError`: every primitive
that hands one to protoIO (files, directories, `BinaryFile`, `OSProcess`,
`Smalltalk getenv:`/`setenv:to:`/`changeDirectory:`, sockets, TLS host
names, `Import from:`) signals `Error` "... must not contain NUL"
(`src/primitives/ArgSupport.h`, `osStringArg`). protoIO did not check, so
another runtime calling it with such a string had the name cut at the NUL.
protoIO 0.4.0, which protoST 0.9.0 requires, refuses it too
(`InvalidArgument`, before any system call), for every runtime; protoST keeps
its own check, which names the selector.

### K7 — Diagnostic text could be cut at a NUL (resolved in 0.9.0)

A `String` holds NUL characters everywhere data travels, and many error texts
embed user data (a selector, a key, the offending part of an argument). The
places that reported a problem by throwing `std::runtime_error` (about 320 in
`src/`) handed the text to Smalltalk through `what()`, a `const char*`, so a
NUL inside the user text shortened the **message** (never data): `ByteArray
fromHex: '0', NUL, 'zz'` reported `ByteArray fromHex: '0`, in `messageText`
and in the uncaught-error report alike.

Since 0.9.0 protoST throws `NativeError` (`src/runtime/NativeError.h`), a
`std::runtime_error` that keeps the full `std::string`; `ClassedErrorSignal`
derives from it. Every site that turns a C++ exception into a Smalltalk error
reads the text through `nativeErrorText` (the native-exception bridge, an
actor message's rejection, the uncaught-error report, the debugger's and the
DAP server's evaluation errors) and builds the Smalltalk string with the
length-aware `makeString`. Every printer of text that can hold user data
writes it with `writeWholeLine` (or `fwrite`), not `%s`, `fputs` or `puts`:
the uncaught error, an unhandled `Warning` (or other resumable exception)
with its `messageText`, the warning for an actor error nobody observed,
parse and compile errors, printed values, the REPL's `:load` and "unknown
command" messages, `halt` without a debugger, `--dump-ast` and `--disasm`
listings (which quote string literals), and `venv info`. The debugger and the
DAP server write `std::string`s to streams and JSON, which keep every byte.
Tests: `12-builtins/native_error_nul_message`,
`12-builtins/native_error_nul_uncaught` (both failed on the build before the
change: the text ended at the NUL), the last case of
`cli_actor_unobserved_error`, and `cli_diagnostics_nul` (the Warning, the
REPL's unknown command and a printed value; its Warning case fails on
main). One printer keeps `what()` on
purpose: the timer thread's report of a failed hand-over, which runs when
memory is short and must not copy, and whose text is never user data.

What remains is outside protoST's errors: a `protoio::Error` has only
`what()` (its text is protoIO's own, and protoST refuses a NUL in any string
it hands to protoIO, K12); protoCore's error factory takes a `const char*`,
so a message created there ends at a NUL; and a protoST `NativeError` that
leaves protoST into another runtime (a protoPython or protoJS program calling
a protoST module, `STForeignSend.cpp` and `STModuleProvider.cpp`, which
re-throw it unchanged) is read there through `what()`, so that runtime sees
its text cut at a NUL. Two protoCore facts, to be
raised there: `ProtoString::createSymbol(ctx, const std::string&)` calls
`c_str()` (`ProtoString.cpp`, around line 1307), so a NUL-bearing
`std::string` is truncated silently (protoST uses `ProtoString::intern`, which
is length-aware, for `asSymbol`); and `fromStdString` truncates the same way.

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
