# protoST 0.9.0 Bytes, Durable Files, Encodings, MessagePack and Timers Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** give protoST immutable binary values, positioned durable files with
ProtoBase's lock, number and digest encodings, MessagePack, and Future
deadlines and timers. This is the ground protoBaseST will need.

**Architecture:** a thin layer over protoIO 0.3.0.
- C++ primitives sit in new files under `src/primitives/` and call protoIO.
- Smalltalk classes in `lib/kernel/` give the Pharo-named surface.
- `ByteArray` is a frozen `ProtoByteBuffer`.
- The timer service is one ProtoThread that only enqueues tasks onto the
  worker pool.

**Tech Stack:** C++20, protoCore ≥ 3.4.0, protoIO ≥ 0.3.0, the conformance
runner (`tests/conformance/run_conformance.sh`), Catch2 unit tests
(`tests/unit`), and CPython 3.12+ with `msgpack` for the interop tests only.

**Spec:** `docs/superpowers/specs/2026-11-09-bytes-files-timers-design.md`. Its
C++ companion is `../protoIO/docs/specs/2026-11-09-binary-storage-design.md`,
and the protoIO plan `../protoIO/docs/plans/2026-11-09-binary-storage-plan.md`
must be finished first.

## Global Constraints

- **No mutable face.**
  - protoST never calls `ProtoByteBuffer::setAt` after creation. The buffer is
    filled once, through `newByteBuffer(data, len)`.
  - `ByteArray>>at:put:` signals `ModificationForbidden`.
- **Pharo names only where Pharo's meaning holds.** Every difference gets a
  numbered deviation entry in `docs/STATUS.md`. The `asString` omission, the
  `nextPutUnsigned:width:bigEndian:` family and `BinaryFile` are known now.
- **Synchronisation:** only actors, Futures and Atom. No user-visible locks.
- **Threads:** ProtoThreads only, never `std::thread` or pthreads. Never hold a
  mutex across a protoCore allocation.
- **I/O wrappers** (`io_prims.cpp:204-225`):
  - `unmanaged()` for `readAt`, `writeAt`, `append`, `size`, `truncate`,
    `tryLock`, `unlock` and `close`;
  - `blocking()` for `lock`, `sync`, `dataSync` and `syncDirectory`.
- **Errors:** `FileDoesNotExist`, `FileAlreadyExists` and `FileSystemError`
  for files; `Error` for bad values; a new `TimeoutError`, a subclass of
  `Error`.
- **Versions:** protoCore floor 3.4.0 unchanged; `PROTOIO_MIN_VERSION` 0.3.0;
  protoST **0.9.0**.
- **Build** on DEV12 with `-j4`; never two protoST builds at once.
- **Visible skips:** any test that cannot verify interop, because python3 or
  msgpack is missing, prints `SKIP: <reason>`. It is never a silent pass.

## Review Focus

1. **Aliasing through a `WriteStream`.** After `s contents` answers `b1`,
   further `s nextPutAll:` calls must not change `b1`. Test it in Task 2.
2. **A `ByteArray` used as a `Dictionary` key and as a `Set` element.** Two
   equal-content values must hit the same entry. Test it in Task 1.
3. **`BinaryFile` used after `close`, and closed twice.** Use signals `Error`;
   a second close is a no-op. Test it in Task 5.
4. **`waitWithin:` on a Future that is already settled, and `within: 0`.**
   The first answers at once, without a timer. The second is rejected, unless
   the Future was already settled. Test it in Task 7.
5. **MessagePack encode of a cyclic `Array`.** It signals `Error`; it must not
   overflow the stack. Test it in Task 6.

Conformance tests use the directive format of `tests/conformance/README.md`:
the first line is `"EXPECT: <text>"` or `"EXPECT-ERROR: <text>"`. Run one
folder with `tests/conformance/run_conformance.sh build_release/protost tests/conformance/15-binary`
(check the runner's usage line first and adjust the arguments to it).

---

### Task 1: `ByteArray` value, literal and dispatch

**Files:**
- Modify: `CMakeLists.txt` (`PROTOIO_MIN_VERSION 0.3.0`).
- Create: `src/primitives/bytes_prims.cpp`, registered from the same place
  that calls `installIoPrimitives`.
- Create: `lib/kernel/bytes.st` (class `ByteArray`), loaded with the kernel.
- Modify: the parser and compiler under `src/frontend/` to add the `#[1 2 255]`
  literal. Each element is an integer literal 0–255; anything else is a
  compile error.
- Modify: class dispatch, so a `ProtoByteBuffer` value answers the `ByteArray`
  class. Follow how `String` values are mapped to their class; grep for
  `isString(` in `src/runtime/`.
- Test: `tests/conformance/15-binary/bytes_basic.st`, `bytes_immutable.st`,
  `bytes_keys.st` and `bytes_literal_errors.st`.

**Interfaces:**
- Produces these primitives:
  - `__bytesNew: n`, `__bytesWithAll: anArray`, `__bytesSize`, `__bytesAt: i`;
  - `__bytesCopyFrom: i to: j`, `__bytesConcat: other`;
  - `__bytesEquals: other`, `__bytesHash`, `__bytesHex`, `ByteArray class >> __fromHex:`.
- Produces this Smalltalk API: `ByteArray new:`, `withAll:`, `size`, `at:`,
  `do:`, `collect:` (an Array), `inject:into:`, `isEmpty`, `copyFrom:to:`,
  `,`, `copyReplaceFrom:to:with:`, `=`, `hash`, `hex`, `fromHex:`,
  `printString` (`#[1 2 255]`), and `at:put:` (signals `ModificationForbidden`).
- Produces this C++ helper for later tasks, in `src/primitives/BytesSupport.h`:
  - `const ProtoObject* makeBytes(ProtoContext*, std::string_view)`;
  - `std::string_view bytesView(ProtoContext*, const ProtoObject*)`, which
    signals `Error` when the argument is not a ByteArray.

  These are the only ways other primitives create or read bytes.

- [ ] **Step 1: write the failing conformance tests.**
  - `bytes_basic.st`, `"EXPECT: 3 255 #[1 0 255] true #[0 255]"`:

    ```smalltalk
    "EXPECT: 3 255 #[1 0 255] true #[0 255]"
    b := ByteArray withAll: #(1 0 255).
    b size printString, ' ', (b at: 3) printString, ' ', b printString, ' ',
      (b = #[1 0 255]) printString, ' ', (b copyFrom: 2 to: 3) printString.
    ```

  - `bytes_immutable.st`: `#[1 2] at: 1 put: 9` gives
    `EXPECT-ERROR: ModificationForbidden`.
  - `bytes_keys.st`: `Dictionary` and `Set` keyed by two separately built
    equal ByteArrays find one entry. It also checks that `(ByteArray new: 3) = #[0 0 0]`
    holds and that a value containing NUL survives `,`.
  - `bytes_literal_errors.st`: `#[256]` gives `EXPECT-ERROR`.
- [ ] **Step 2:** build (`cmake --build build_release -j4`) and run the folder.
  Expected: FAIL (unknown class, parse error).
- [ ] **Step 3:** implement the primitives. Create each value through
  `makeBytes` only. `__bytesHash` is FNV-1a 64 over the contents, for
  determinism. Implement the parser literal and the class dispatch.
- [ ] **Step 4:** run the folder and the whole conformance suite (`ctest
  --test-dir build_release -R conformance`). Expected: the new tests PASS and
  nothing regresses.
- [ ] **Step 5:** commit with the message
  `ByteArray: an immutable value over ProtoByteBuffer, with the #[...] literal`.

### Task 2: Byte streams, UTF-8, and migrating existing binary APIs

**Files:**
- Modify: `lib/kernel/bytes.st`, plus the stream classes so that
  `WriteStream on: ByteArray new` and `ByteArray >> readStream` work.
- Modify: `lib/kernel/io.st:72,282-285` and the matching code in
  `src/primitives/io_prims.cpp`. `nextBytes:` and `binaryContents` answer a
  ByteArray. `binaryContents:` accepts a ByteArray, or an Array of 0–255 with a
  one-line deprecation warning on stderr, printed once per process.
- Test: `tests/conformance/15-binary/bytes_streams.st` and
  `bytes_utf8.st`; update any existing `14-io` test that compared with an
  Array.

**Interfaces:**
- Consumes: Task 1's `makeBytes` and `bytesView`.
- Produces:
  - `WriteStream` on bytes, with `nextPut: anInteger`, `nextPutAll: aByteArray`
    and `contents`. `contents` answers a new frozen ByteArray, and the
    stream's own accumulator is never shared with an answered value.
  - `ReadStream` on bytes: `next`, `next: n` (a ByteArray), `atEnd`,
    `position`, `skip:`.
  - `String >> utf8Encoded` (a ByteArray) and `ByteArray >> utf8Decoded`
    (strict; invalid UTF-8 signals `Error`).

- [ ] **Step 1: failing tests.**
  - `bytes_streams.st` builds `b1 := s contents`, appends more, and asserts
    that `b1` is unchanged while `s contents` has grown. It also reads it back
    with `next:`.
  - `bytes_utf8.st` round-trips `'añ€'` and a NUL-containing string through
    `utf8Encoded`/`utf8Decoded`; `#[255] utf8Decoded` gives `EXPECT-ERROR`.
  - A `14-io` file test writes `#[0 1 2 0]` with `binaryContents:` and reads
    back an equal ByteArray.
- [ ] **Step 2:** run the folders `15-binary` and `14-io`. Expected: the new
  tests FAIL.
- [ ] **Step 3:** implement. Build the String↔bytes conversion from
  `(data, length)`, never through `c_str()`, which is the NUL truncation the
  audit found.
- [ ] **Step 4:** run both folders. Expected: PASS.
- [ ] **Step 5:** commit with the message
  `ByteArray streams and UTF-8; binary file APIs answer ByteArray`.

### Task 3: Numbers in bytes

**Files:**
- Modify: `src/primitives/bytes_prims.cpp` and `lib/kernel/bytes.st`.
- Create: `tests/conformance/15-binary/gen_vectors.py`. It converts
  `../protoIO/tests/vectors/*.json` into Smalltalk module files
  `tests/conformance/15-binary/_vectors_<name>.st`, each defining a class
  method that answers an Array of Arrays of literals. Both the script and its
  output are committed.
- Test: `tests/conformance/15-binary/packing_vectors.st` and `packing_errors.st`.

**Interfaces:**
- Consumes: `protoio::encoding::{getUint,getInt,getFloat64,getFloat32,putUint,putInt,putFloat64,putFloat32}`.
- Produces, with 1-based indices:
  - on ByteArray: `unsignedByteAt:`, `unsignedShortAt:bigEndian:`,
    `unsignedLongAt:bigEndian:`, `unsignedLong64At:bigEndian:`,
    `signedShortAt:bigEndian:`, `signedLongAt:bigEndian:`,
    `signedLong64At:bigEndian:`, `float32At:bigEndian:`,
    `float64At:bigEndian:`;
  - on a byte WriteStream: `nextPutUnsigned:width:bigEndian:`,
    `nextPutSigned:width:bigEndian:`, `nextPutFloat32:bigEndian:`,
    `nextPutFloat64:bigEndian:`.
- Produces for Tasks 4 and 6: `Vectors<Name> all`, the generated module API,
  imported with `Import from: '_vectors_<name>.st'`.

- [ ] **Step 1: failing tests.**
  - `packing_vectors.st` runs every `packing.json` entry in both directions
    and prints the number of entries that failed. `"EXPECT: 0"`, and the
    test also prints the count checked, so an empty vector set cannot pass.
  - `packing_errors.st` checks that `nextPutUnsigned: 256 width: 1 bigEndian: false`
    and `#[1] unsignedShortAt: 1 bigEndian: true` each give `EXPECT-ERROR`.
- [ ] **Step 2:** run `python3 -I tests/conformance/15-binary/gen_vectors.py`,
  then run the folder. Expected: FAIL.
- [ ] **Step 3:** implement through protoIO. Floats go through bit patterns,
  so NaN payloads and -0.0 survive.
- [ ] **Step 4:** run the folder. Expected: PASS, with a non-zero count checked.
- [ ] **Step 5:** commit with the message
  `ByteArray: integer and IEEE 754 reading and writing, checked against CPython vectors`.

### Task 4: Digests and base64

**Files:**
- Modify: `src/primitives/bytes_prims.cpp` and `lib/kernel/bytes.st`.
- Create: `lib/kernel/digest.st` (classes `CRC32` and `HMAC`).
- Test: `tests/conformance/15-binary/digest_vectors.st` and `base64.st`.

**Interfaces:**
- Consumes: `protoio::digest::{crc32,crc32Combine,blake2b,sha256,hmacSha256}`
  and `protoio::encoding::{base64Encode,base64Decode}`.
- Produces:
  - `ByteArray >> crc32`, `crc32Continuing: anInteger`, `blake2b: size`,
    `blake2b:key:`, `sha256`, `base64Encoded` (a String);
  - `String >> base64Decoded` (a ByteArray);
  - `CRC32 class >> combine:with:length:`;
  - `HMAC class >> sha256Key:message:`.

- [ ] **Step 1: failing tests.**
  - `digest_vectors.st` runs every vector of `crc32`, `blake2b` and
    `sha256_hmac`, and prints failures and count, as in Task 3.
  - `base64.st` runs the vectors, and checks that `'ab!=' base64Decoded` gives
    `EXPECT-ERROR`.
- [ ] **Step 2:** regenerate the vectors if needed and run the folder.
  Expected: FAIL.
- [ ] **Step 3:** implement.
- [ ] **Step 4:** run the folder. Expected: PASS.
- [ ] **Step 5:** commit with the message `ByteArray: CRC32, BLAKE2b, SHA-256, HMAC and base64`.

### Task 5: `BinaryFile`, `syncDirectory:` and `replace:with:`

**Files:**
- Create: `src/primitives/binfile_prims.cpp` and `lib/kernel/binfile.st`.
- Modify: `lib/kernel/io.st` (`FileSystem class >> syncDirectory:` and
  `replace:with:`).
- Test: `tests/conformance/15-binary/binfile_basic.st`, `binfile_errors.st` and
  `binfile_lock.st`; `tests/cli/test_cli_binary_lock.sh` (cross-process and
  ProtoBase peer).

**Interfaces:**
- Consumes the `protoio::file` additions (protoIO Task 1) and `makeBytes` and
  `bytesView`.
- Produces:
  - `BinaryFile class >> open: aPath options: anArrayOfSymbols`. The symbols
    are `#read #write #create #exclusive #truncate #append`; an unknown one
    signals `Error`.
  - Instance methods: `append:` (answers the start offset), `readAt:size:`,
    `writeAt:bytes:`, `size`, `truncateTo:`, `sync`, `dataSync`, `tryLock`
    (a Boolean), `lock`, `unlock` and `close`.
  - The descriptor is owned by an `FdState`-like holder, as `io_prims.cpp`
    already does. Close is idempotent, and any other use after close signals
    `Error` with the text `BinaryFile is closed`.
- The class comment states the contract: one appender at a time, any number
  of readers, no position.

- [ ] **Step 1: failing tests.**
  - `binfile_basic.st`:
    1. `#create #exclusive`, then `append:` twice; the offsets answer 0 and 4.
    2. `readAt: 0 size: 8` returns both appends.
    3. `writeAt: 2 bytes: #[0]`.
    4. `size`, `truncateTo:`, `sync`, `dataSync`, and
       `FileSystem syncDirectory:` and `replace:with:`.
  - `binfile_errors.st`: a second `#exclusive` open gives
    `EXPECT-ERROR: FileAlreadyExists`, and a read after `close` gives
    `EXPECT-ERROR: BinaryFile is closed`. A double close is a no-op (a separate
    `EXPECT` file).
  - `binfile_lock.st`: two descriptors in one process; `tryLock` answers true,
    then false, then true after `unlock`.
  - `test_cli_binary_lock.sh`:
    - (a) one protoST process holds `lock` while a second answers
      `tryLock = false`;
    - (b) `../protoIO/tests/ownerlock_peer.py` holds the ProtoBase lock while
      protoST answers `false`. Without `python3` it prints
      `SKIP: python3 not found, ProtoBase lock interop NOT verified`.

    Register it with the existing CLI test pattern in `tests/CMakeLists.txt`.
- [ ] **Step 2:** build and run the new tests. Expected: FAIL.
- [ ] **Step 3:** implement, choosing `unmanaged()` or `blocking()` per call
  as in Global Constraints.
- [ ] **Step 4:** run the new tests and the `14-io` folder. Expected: PASS.
- [ ] **Step 5:** commit with the message
  `BinaryFile: positioned, durable files with the ProtoBase-compatible lock`.

### Task 6: MessagePack

**Files:**
- Create: `src/primitives/msgpack_prims.cpp` and `lib/kernel/msgpack.st`
  (classes `MessagePack`, `MessagePackMap` and `MessagePackExt`).
- Test: `tests/conformance/15-binary/msgpack_vectors.st`, `msgpack_mapping.st`
  and `msgpack_errors.st`.

**Interfaces:**
- Consumes: `protoio::msgpack::{Writer,Handler,read,Limits}`, `makeBytes` and
  `bytesView`.
- Produces:
  - `MessagePack class >> encode:`, `decode:`, `decode:strictKeys:`,
    `decodeFirst:` (an Association of the object and the bytes consumed) and
    `decodeOrdered:`;
  - `MessagePackMap class >> withAll: anArrayOfAssociations`, with
    `associations`;
  - `MessagePackExt class >> type:data:`, with `type` and `data`.
- Mapping: the table in spec §4.5. Symbols encode as str. An integer outside
  [−2^63, 2^64−1] signals `Error`. An unsupported class signals `Error` with
  the text `MessagePack cannot encode a <ClassName>`.
- Encode walks the graph in C++, with an identity set of the containers in
  the current path: meeting one again signals `Error` with the text
  `MessagePack: cyclic structure`.

- [ ] **Step 1: failing tests.**
  - `msgpack_vectors.st` handles every `msgpack.json` vector. Decoding with
    `decodeOrdered:` must match the expected value tree. Re-encoding the
    ordered form must give the exact bytes. It prints failures and count.
  - `msgpack_mapping.st` round-trips nil, booleans, small and 2^63 integers, a
    Float, a String with NUL, a ByteArray, an Array, a Dictionary, a
    MessagePackMap (order preserved) and an Ext. It also checks that a Symbol
    decodes as a String.
  - `msgpack_errors.st` (separate files, one error each):
    - trailing bytes on `decode:`;
    - `2 raisedTo: 64`;
    - `Object new`;
    - a cyclic Array;
    - the map `{1: 2}` under default strict keys;
    - the same with `strictKeys: false` decodes (an `EXPECT` file).
- [ ] **Step 2:** build and run. Expected: FAIL.
- [ ] **Step 3:** implement. The reader `Handler` builds objects with an
  explicit stack, without recursion.
- [ ] **Step 4:** run. Expected: PASS.
- [ ] **Step 5:** commit with the message
  `MessagePack: encode and decode over protoIO, ordered maps and ext`.

### Task 7: Timers and deadlines

**Files:**
- Create: `src/runtime/TimerService.h` and `TimerService.cpp`; the service is
  owned by `STRuntime` and started lazily.
- Create: `src/primitives/timer_prims.cpp`.
- Modify: `lib/kernel/future.st` (`after:`, `within:`, `waitWithin:`) and the
  exception classes (`TimeoutError`, a subclass of `Error`).
- Test: `tests/conformance/10-actors/future_after.st`, `future_within.st`,
  `future_within_settled.st`, `future_within_actor.st`; `tests/unit/test_timer_service.cpp`.

**Interfaces:**
- Consumes: `STRuntime::enqueueTask(ctx, task)`
  (`include/protoST/STRuntime.h:151`) and the existing settle path
  (`future_prims.cpp`, `settleFuture`).
- Produces:
  - `TimerService::schedule(ProtoContext*, std::chrono::steady_clock::time_point, const ProtoObject* future, bool reject)`.
    The timer thread only builds and enqueues a task whose execution, on a
    worker, settles `future` (resolved with nil, or rejected with a
    `TimeoutError`). The future must be rooted while it is pending: use one
    pinned structure for all pending timers, not one root per timer.
  - Smalltalk: `Future class >> after: ms`, `Future >> within: ms` and
    `Future >> waitWithin: ms`.
- Exit: pending timers are dropped at process exit, and the service never
  keeps the process alive.

- [ ] **Step 1: failing tests.**
  - `future_after.st`: `(Future after: 50) wait` answers nil, and the
    monotonic time elapsed is ≥ 50 ms.
  - `future_within.st`: `[(Future new) waitWithin: 50] on: TimeoutError do: [:e | 'timeout']`
    gives `"EXPECT: timeout"`.
  - `future_within_settled.st`: a resolved Future, `waitWithin: 0`, answers
    its value. A Future resolved after 20 ms, `waitWithin: 2000`, answers its
    value in well under 2000 ms.
  - `future_within_actor.st`: 200 actors each `waitWithin: 100` on a never
    settled Future. All of them time out, and wall time is < 5 s, which proves
    the waits suspend instead of blocking workers.
  - `test_timer_service.cpp`: a callback that blocks for 500 ms does not delay
    a second timer due 50 ms later by more than 100 ms. Run it under
    `PROTOST_WORKERS=2` or more.
- [ ] **Step 2:** build and run. Expected: FAIL.
- [ ] **Step 3:** implement. The service is one ProtoThread with a deadline
  min-heap on `steady_clock`. Its internal mutex only guards the heap, and it
  is released before any protoCore allocation; tasks are built outside it.
- [ ] **Step 4:** run the new tests and the whole `10-actors` folder.
  Expected: PASS. Repeat `future_within_actor.st` 20 times; it must have no
  flakes.
- [ ] **Step 5:** commit with the message
  `Futures: after:, within:, waitWithin: and TimeoutError on a timer service`.

### Task 8: Cross-language round trip and concurrency stress

**Files:**
- Create: `tests/cli/test_cli_binary_interop.sh`, `tests/cli/interop_peer.py`,
  and `tests/conformance/15-binary/binfile_stress.st`.

**Interfaces:**
- Consumes everything above.

- [ ] **Step 1: write the tests.**
  - `test_cli_binary_interop.sh`:
    1. `interop_peer.py write <dir>` writes `data.msgpack`, a nested value with
       every type and a multi-key map, encoded with
       `msgpack.packb(use_bin_type=True)`.
    2. protoST reads it with `decodeOrdered:`, re-encodes it, and writes
       `back.msgpack`. `interop_peer.py check` asserts that
       `unpackb(back) == unpackb(original)`; the bytes are equal too, because
       the order was preserved.
    3. The other direction: protoST writes and Python checks the value.
    4. A record framed as ProtoBase frames it, as
       `payload_len:u32 LE | crc32:u32 LE | payload`. *My call:* a simplified
       framing that is not ProtoBase's exact record format, which belongs to
       protoBaseST. One side writes it and the other verifies it.

    Without `python3` or `msgpack` it prints `SKIP: <reason>`.
  - `binfile_stress.st`: 16 actors each perform 1,000 `readAt:size:` calls on
    random offsets, while one actor makes 1,000 appends of 64-byte records
    that carry their own CRC32. At the end, every record verifies and no read
    returned bytes outside the file. It prints the count of records checked.
- [ ] **Step 2:** run both. Expected: PASS (or a visible SKIP for interop).
- [ ] **Step 3:** commit with the message
  `Tests: CPython round trip and concurrent BinaryFile stress`.

### Task 9: Documentation and release 0.9.0

**Files:**
- Modify: `docs/LANGUAGE.md`, with a new chapter "Bytes, binary files and
  encodings", and §10.3 for `after:`, `within:` and `waitWithin:`.
- Modify: `docs/STATUS.md`. Add these deviations:
  - no `ByteArray >> asString`;
  - `ByteArray` is immutable;
  - the `nextPut...width:` writers;
  - `BinaryFile` has no position;
  - Symbols encode as str in MessagePack.

  Also refresh the test count and the verification date.
- Modify: `docs/tutorial/` (add a short section to the input-and-output
  chapter), `CHANGELOG.md`, and `CMakeLists.txt` (`VERSION 0.9.0`).

- [ ] **Step 1:** run the full suite twice:
  `ctest --test-dir build_release -j1 --output-on-failure`. Expected: all
  PASS; list any SKIP lines in the commit message.
- [ ] **Step 2:** write the docs, then commit with the message
  `protoST 0.9.0: bytes, binary files, encodings, MessagePack and timers`.
- [ ] **Step 3:** push the branch and open a PR so that CI runs on Linux,
  macOS and Windows. Expected: green on all three. Do not tag or release
  without the maintainer.
