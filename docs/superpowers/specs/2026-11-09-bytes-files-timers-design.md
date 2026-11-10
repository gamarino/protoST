# protoST: bytes, durable files, encodings, MessagePack and timers

Status: **approved** (2026-11-09). Author: Gustavo Marino, with Claude.
Companion: the C++ layer is specified in
`protoIO/docs/specs/2026-11-09-binary-storage-design.md`. Read its §1–§3 first.

## 1. Intent

protoBaseST will be a perennial object database for protoST, in the tradition
GemStone opened. It must share ProtoBase's on-disk format. Before it can be
designed, protoST needs the ground the Python version takes for granted:
- binary values;
- durable, positioned files with an inter-process lock;
- integer, float and hash encodings;
- MessagePack;
- waiting with a deadline.

This spec gives protoST that ground. Everything here is general-purpose:
nothing knows about ProtoBase.

**Principle (maintainer, 2026-11-09): conceptual clarity first, familiar names
where possible.**
- protoCore's model is immutable, and new types must not wear a mutable face.
- A Pharo name is used only where Pharo's meaning holds. Otherwise a clear
  name is chosen and the difference is recorded as a deviation in
  `docs/STATUS.md`.
- Synchronisation is **only actors, Futures and Atom: no locks**.

## 2. Goals, non-goals, anti-goals

**Goals**
- A Smalltalker feels on known ground (`ByteArray`, `readStream`,
  `utf8Encoded`, `base64Encoded`, `crc32`) and still sees plainly that bytes
  are values.
- protoBaseST can write the bytes ProtoBase writes and read what it reads,
  proven by the protoIO vectors re-run from Smalltalk, plus a cross-language
  file round trip with CPython.
- Durable files behave as ProtoBase's: positioned I/O, fsync, directory sync,
  and the same lock, so a Python ProtoBase process and a protoST process
  exclude each other.
- Deadlines for Futures, and timers, as the building blocks for leases later.

**Non-goals**
- ProtoBase's tags, `order_key`/`hash_key`, record framing: protoBaseST.
- C3 linearization, dynamic `super`, namespaces, FFI: language decisions,
  separate.
- Binary HTTP bodies and non-blocking sockets: follow-ups (needed for S3 and
  the cluster later).
- Mutexes, semaphores, condition variables.

**Anti-goals**
- A `ByteArray` that can be changed in place, through any path, including
  streams that alias it.
- Bytes that truncate or mangle at a NUL, or a String↔bytes conversion that
  is not UTF-8.
- A timeout that leaves a hidden thread or a blocked worker behind, or that
  fails silently.

## 3. Constraints

- The protoIO companion spec provides every OS and encoding primitive. protoST
  adds bindings in `src/primitives/` and classes in `lib/`. It does not
  re-implement them.
- Each blocking call picks the right existing wrapper (`io_prims.cpp:204-225`).
  - Local, bounded file calls (`readAt`, `writeAt`, `append`, `size`,
    `truncate`, `tryLock`) use `unmanaged()`.
  - Calls that can wait without bound (`lock`, `sync`, `dataSync`,
    `syncDirectory`) use `blocking()`, so the worker pool grows.
  - Never hold a mutex across a protoCore allocation.
- Threads are ProtoThreads, never `std::thread` or pthreads.
- protoCore floor unchanged (3.4.0), unless the builder finds a needed fix.

## 4. The design

### 4.1 `ByteArray`: an immutable value over `ProtoByteBuffer`

`ByteArray` is backed by protoCore's `ProtoByteBuffer`
(`protoCore/headers/protoCore.h:2179`): contiguous, opaque octets 0–255, NUL
safe. **protoST never calls `setAt`.** The buffer is filled exactly once, at
creation, and is frozen from then on. Decided by the maintainer over a new
rope-of-bytes type in protoCore. Database payloads are records and pages of a
few KB, which contiguous storage serves best.

Its behaviour follows the `String` precedent (D34):

| Selector | Meaning |
|---|---|
| `#[1 2 255]` | literal (Pharo syntax). The parser does not have it yet; the builder adds it. |
| `ByteArray new: n` | n zero bytes (an immutable value) |
| `ByteArray withAll: aCollection` | from integers 0–255; anything else signals `Error` |
| `size`, `at:`, `do:`, `collect:` (answers an `Array`), `inject:into:`, `isEmpty` | as Pharo; `at:` answers an Integer 0–255 |
| `at:put:` | signals `ModificationForbidden`, like `String` |
| `copyFrom:to:`, `,`, `copyReplaceFrom:to:with:` | answer new values |
| `=`, `hash` | by contents; `hash` deterministic within a run. `ProtoByteBuffer`'s built-in hash is identity, so protoST computes both itself. |
| `readStream` | a `ReadStream` over the value |
| `WriteStream on: ByteArray new` → `nextPut:`, `nextPutAll:` and `contents` | builds a new value; `contents` answers a frozen `ByteArray`. The stream accumulates in its own buffer and never aliases a value it handed out. |
| `utf8Decoded` / `String >> utf8Encoded` | strict UTF-8 conversion; invalid input signals `Error` (Pharo names, Pharo meaning) |
| `asString` | *Not provided*: Pharo's is Latin-1, and that would mislead. Recorded as a deviation. |
| `hex` / `ByteArray fromHex:` | lowercase hex, for tests and debugging |
| `printString` | `#[1 2 255]` |

**Breaking change:** today binary data is an `Array` of SmallIntegers
(`FileReference >> binaryContents`, `IOStream >> nextBytes:`). These now answer
and accept `ByteArray`. Accepting a plain `Array` of 0–255 as input stays
possible for one release, with a deprecation note. Recorded in the changelog
and in STATUS.md.

**Why `ByteArray` has no `setAt` (maintainer review, 2026-11-09).**
A mutable byte value would cause four problems, and restricting it to I/O
cannot prevent them, because a mutable object can always escape:
- a torn read when two threads share it;
- a record whose CRC was computed over bytes that changed before the write;
- a content-hashed `Dictionary` key that silently corrupts its dictionary;
- a committed value that changes behind a pointer-keyed cache, breaking
  perenniality.

**Future extension, only if a measurement demands it: `IOBuffer`.** It is a
separate, mutable type for reusing read buffers (Pharo's `readInto:` pattern):
- it has a single owner and is never shared between actors;
- it has no `hash`, so it cannot become a key;
- it converts to `ByteArray` by copy, which is the only way its bytes leave
  I/O.

Allocation is cheap under protoCore's GC, so this is not built now.

### 4.2 Reading and writing numbers in bytes

Indices are 1-based, as everywhere in Smalltalk. Pharo names where they exist:

| On `ByteArray` (read) | On a byte `WriteStream` (write) |
|---|---|
| `unsignedByteAt:`, `unsignedShortAt:bigEndian:`, `unsignedLongAt:bigEndian:`, `unsignedLong64At:bigEndian:` | `nextPutUnsigned: anInteger width: n bigEndian: aBoolean` |
| `signedShortAt:bigEndian:`, `signedLongAt:bigEndian:`, `signedLong64At:bigEndian:` | `nextPutSigned: anInteger width: n bigEndian: aBoolean` |
| `float64At:bigEndian:`, `float32At:bigEndian:` | `nextPutFloat64: aFloat bigEndian:`, `nextPutFloat32:bigEndian:` |

- **Out of range or out of bounds:** signals `Error`, never wraps silently.
- **The writing side:** *my call* to keep the API small. A width parameter
  replaces Pharo's family of `nextNumber:put:` / `uint32:`, because those names
  carry Pharo stream semantics that differ here. Recorded in STATUS.md.

### 4.3 Digests and base64

| Selector | protoIO |
|---|---|
| `aByteArray crc32` / `crc32Continuing: anInteger` | `crc32` |
| `CRC32 combine: crcA with: crcB length: lenB` | `crc32Combine` |
| `aByteArray blake2b: digestSize` / `blake2b: size key: aByteArray` | `blake2b` |
| `aByteArray sha256` / `HMAC sha256Key: k message: m` | OpenSSL |
| `aByteArray base64Encoded` (a String) / `String >> base64Decoded` (a ByteArray) | Pharo names |

No incremental hasher is exposed in Smalltalk. ProtoBase hashes small keys
with BLAKE2b, and it continues CRCs with `crc32Continuing:` and `CRC32
combine:...`, which are already values. A mutable hasher object would break the
model, and an immutable one would copy the OpenSSL context on every update.
*My call:* add one later only if a real need appears. The C++ incremental
classes stay in protoIO for other runtimes.

### 4.4 `BinaryFile`: positioned, durable files

A new class, deliberately **without a hidden position**. Every read and write
names its offset, so a `BinaryFile` can be shared between actors with no
cursor to race on. *My call on the name.* Pharo's `File`/`FileStream`
carry a cursor, and reusing those names would mislead.

```smalltalk
f := BinaryFile open: 'data/seg-0001' options: #(#read #write #create #exclusive).
off := f append: bytes.          "answers the offset where bytes start"
chunk := f readAt: off size: 64. "a ByteArray; shorter only at end of file"
f writeAt: 0 bytes: header.
f size. f truncateTo: n.
f sync. f dataSync.
f tryLock. f lock. f unlock.     "the same lock ProtoBase takes (flock / LockFileEx byte 0)"
f close.
FileSystem syncDirectory: 'data'.
FileSystem replace: 'root.tmp' with: 'root'.   "atomic, os.replace semantics"
```

- **Options:** one symbol per flag (`#read #write #create #exclusive #truncate
  #append`); an unknown symbol signals `Error`.
- **Errors:** the existing `FileDoesNotExist` / `FileAlreadyExists` /
  `FileSystemError` hierarchy (`lib/kernel/io.st:16`).
- **Contract: one appender at a time per `BinaryFile`; any number of
  readers.** There is no portable atomic append-with-offset, so this is
  documented. A protoBaseST segment has a single writer actor. *My call.*
- **Closing:** a file closed twice is a no-op; any use after close signals
  `Error`. Nothing ever closes silently: closing happens on `close` or at
  process exit, not when the GC collects the object.
- **Blocking `lock`:** it blocks a worker inside `BlockingIO`, and the pool
  grows as it already does for blocking I/O. Documented.
- `FileReference` keeps its whole-file conveniences.

### 4.5 MessagePack

| Selector | Meaning |
|---|---|
| `MessagePack encode: anObject` | a `ByteArray` |
| `MessagePack decode: aByteArray` | the object; trailing bytes signal `Error` |
| `MessagePack decodeFirst: aByteArray` | an `Association` of the object and the bytes consumed |
| `MessagePack decodeOrdered: aByteArray` | as `decode:`, but maps become `MessagePackMap`s holding their `Association`s in wire order |
| `MessagePackMap withAll: associations` | a value that encodes as a map in exactly that order; the ordered encode counterpart (*my call*) |

Map keys are strict by default: only str and bin keys decode, as in
msgpack-python 1.x and every ProtoBase call site. `decode:strictKeys: false`
relaxes this explicitly. Re-encoding a decoded multi-key `Dictionary` matches
CPython's *values*, not necessarily its bytes. `decodeOrdered:` and
`MessagePackMap` reproduce the bytes when that matters.

`decodeOrdered:` exists because protoCore's attribute order is unspecified, so
a `Dictionary` cannot preserve wire order. A higher layer that needs it (for
example ProtoBase's `@map`) asks for it explicitly.

**Mapping** (encoding → wire → decoding):

| Smalltalk | Wire | Back |
|---|---|---|
| `nil`, `true`, `false` | nil, bool | same |
| Integer in [−2^63, 2^64−1] | smallest int/uint | Integer |
| Integer outside that range | signals `Error` (protoBaseST encodes big integers itself, as ProtoBase's `@int`) | — |
| `Float` | float64 | Float (float32 widened) |
| `String` | str (UTF-8) | String |
| `Symbol` | str | String |
| `ByteArray` | bin | ByteArray |
| `Array`, `OrderedCollection` | array | Array |
| `Dictionary` | map | Dictionary |
| `MessagePackExt type: n data: bytes` | ext | MessagePackExt |
| anything else | signals `Error` naming the class | — |

Encoding walks the object graph in C++ and feeds protoIO's `Writer`, so there
are no per-element bytecodes. Decoding builds objects from protoIO reader
events. A cyclic graph signals `Error`, and depth limits come from protoIO's
`Limits`.

### 4.6 Deadlines and timers: only Futures

| Selector | Meaning |
|---|---|
| `Future after: ms` | a Future resolved with `nil` after `ms` milliseconds (monotonic clock) |
| `aFuture within: ms` | a new Future: settles like `aFuture` if that happens first, otherwise is rejected with `TimeoutError` |
| `aFuture waitWithin: ms` | `(aFuture within: ms) wait`: answers the value or signals `TimeoutError` |
| `TimeoutError` | a new `Error` subclass |

- **Periodic and delayed work** compose from these, for example
  `(Future after: 500) thenDo: [anActor tick]`. No other timer API. *My call:*
  this keeps one concept.
- **The timer thread never runs Smalltalk code.** In protoST the thread that
  settles a Future runs its callbacks (`future_prims.cpp`, `settleFuture`). So
  when a deadline expires, the timer service hands the settle to the worker
  pool as a ready task. Callbacks then run on a worker, and a slow or waiting
  callback cannot delay other timers. This needs a small scheduler entry
  point ("run this settle on a worker"). *My call:* the alternative would make
  the timer thread a full interpreter thread.
- **The original Future** is not cancelled by a timeout. It may still settle
  later, and a Future settles only once, so a late result is ignored by the
  derived one.
- **The timer service** is one ProtoThread per process, started on first use,
  running a deadline heap on the monotonic clock. It settles Futures through
  the same path actors use.
  - It must never hold its internal mutex across a protoCore allocation.
  - It must not keep the process alive at exit: pending timers are dropped,
    and that is documented.
- **Waiting:** `waitWithin:` inside an actor suspends cooperatively, like
  `wait`. On the main thread it blocks that OS thread, like `wait`.
- **`sleep:`** stays a test helper.

### 4.7 Tests

- **Conformance:** a new folder, `tests/conformance/15-binary/`.
  - ByteArray semantics, including `ModificationForbidden`, NUL bytes and
    non-aliasing streams.
  - Every protoIO vector file re-run from Smalltalk.
  - BinaryFile operations and errors.
  - The lock: a protoST process against a second protoST process, and against
    CPython running ProtoBase's `ownerlock.py` logic when Python is available.
    That second case is skipped with a visible notice otherwise, never
    silently passed.
  - MessagePack mapping and errors.
  - Timers and deadlines, with tolerances generous enough for CI.
- **Cross-language round trip:**
  - CPython `msgpack` writes a file, protoST reads and re-encodes it, and
    CPython reads it back and compares. The same in the other direction.
  - Also a CRC32-framed record written by one side and verified by the other.
- **Concurrency stress:** many actors reading one `BinaryFile` at different
  offsets while one appends; timers firing while actors wait.

### 4.8 Release

protoST **0.9.0**, requiring protoIO 0.3.0. LANGUAGE.md gains a chapter, and
the tutorial gains a short "bytes and files" section.

## 5. Left to the builder

- Exact internal representation of the frozen `ByteArray` wrapper, and how
  the VM dispatches on `ProtoByteBuffer`.
- How `WriteStream` buffers bytes before freezing them.
- Timer heap and wake-up mechanism, within the ProtoThread and safepoint rules.
- Error message wording.

## 6. Decisions settled after review

- **Symbols encode as str, by necessity.** Per D35 a symbol of up to 6 bytes
  *is* a String at runtime (`#foo class == String`), so any other rule would
  treat `#foo` and `#foobarbaz` differently. ProtoBase's symbols travel as
  `{"@sym": name}` one level up, in protoBaseST. (The draft's "as in Pharo"
  was wrong: Pharo's `#foo = 'foo'` is false.)
- **Map keys are strict by default; map order is explicit** (§4.5).
- **One appender per file** (§4.4).
- **The timer thread only schedules; workers run callbacks** (§4.6).
- **No Smalltalk-level incremental hasher** (§4.3).
- **Wrapper choice per call:** `unmanaged()` versus `blocking()` (§3).
