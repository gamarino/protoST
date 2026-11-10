# Task 8 report: CPython round trip and BinaryFile stress

Branch `feature/binary-storage`. Commit hash in the reply.

## Files

- `tests/cli/interop_peer.py` (CPython side; plain `python3`, `msgpack` 1.1.2 from user-site, so no `-I`).
- `tests/cli/test_cli_binary_interop.sh` (driver plus the protoST side as a heredoc script). Registered in the
  root `CMakeLists.txt` as `cli_binary_interop` (`SKIP_RETURN_CODE 77`, `TIMEOUT 60`), next to `cli_binary_lock`.
  Without python3, or when `python3 -c "import msgpack"` fails, it prints `SKIP: ...` and exits 77.
- `tests/conformance/15-binary/binfile_stress.st`.

## What is exchanged

CPython -> protoST (`interop_peer.py write`):
1. `data.msgpack`: `packb(sample, use_bin_type=True)`, a 12-key map with deliberately unsorted keys (including the
   empty key) holding nil, true, false, 23 integers across every width boundary (to 2^64-1 and -2^63), floats
   (1.5, -0.0, pi, 1e300), a non-ASCII str, bin, empty bin, an ExtType, and nested maps/arrays. protoST reads it with
   `decodeOrdered:`, re-encodes with `encode:` and writes `back.msgpack`; the bytes are identical to the original
   (checked on both sides; CPython also compares `unpackb(back) == unpackb(original)`).
2. `facts_py.bin` (150 bytes): `struct.pack(">BHIQ")`, `("<bhiqfd")`, `(">qfd")`, `zlib.crc32` of the document,
   `blake2b(digest_size=20)`, keyed `blake2b(digest_size=32, key=16 bytes)`, `sha256`. protoST reads every field
   with the `...At:bigEndian:` selectors and compares with its own `crc32`, `blake2b:`, `blake2b:key:`, `sha256`.
3. `records_py.bin`: 50 records framed `payload_len u32 LE | crc32 u32 LE | payload` (payload sizes 0..294).
   protoST verifies each CRC and length. (The framing is the simplified one the brief calls for, not ProtoBase's
   exact record format.)

protoST -> CPython (`interop_peer.py check`):
1. `out.msgpack`: the same document built as a `MessagePackMap` (so order is deterministic; a Dictionary's order is
   unspecified). CPython requires `unpackb(out) == sample` AND `out == packb(sample)` byte for byte (this includes the
   ext encoding, the integer widths and the float64 choice).
2. `facts_st.bin`: the same layout written with the byte WriteStream packing selectors and digests; CPython requires
   byte equality with its `struct`/`zlib`/`hashlib` result.
3. `records_st.bin`: 50 frames appended with `BinaryFile append:`; CPython parses every frame (length, zlib CRC) and
   compares the whole file with its own reference bytes.

No interop mismatch was found in Tasks 1-7: the first run of the round trip passed.

## Stress test (`binfile_stress.st`)

Parameters: a 65,536-byte known-pattern prefix (byte i = i mod 251); one appender actor making 1,000 appends of framed
records (16 to 64 bytes each, 39,989 bytes in total); 16 reader actors each making 1,000 `readAt:size:` calls (size
1..128) at random offsets (seeded `Random`, offsets up to prefix + 20,000, so reads straddle the prefix, the growing
tail and the end of the file) while the appends happen; 16,000 reads in total. Assertions, all independent of
scheduling: every append offset is the end of the previous record (unique, and offsets plus sizes tile from 65,536 to
`size` with no gap or overlap); every record reads back with a valid length and CRC32; each read answers at most the
bytes asked, exactly the bytes asked and exactly the pattern when it lies inside the prefix, and the pattern bytes
for any part of a read inside the prefix. No sleep; the actors are synchronised only by `wait` on their Futures. Temp
directory removed in `ensure:` (nothing left in /tmp). Runtime about 1.3 s. 10 consecutive runs: 10 of 10 pass.

Decision (design finding, not a bug): my first version used four appender actors on one BinaryFile. It failed
(offsets overlapped, records overwritten). That is the documented contract: protoIO `appendData` and the class
comment of `BinaryFile` say ONE APPENDER AT A TIME per file, because the answered offset is the end of the file just
before the write. The brief also asks for a single appender. The test therefore uses one appender and many readers.

## Mutation checks (the tests can fail)

- Interop: `'empty key'` -> `'empty keY'` in the protoST document: `FAIL: out.msgpack decodes to another value`;
  a float32 `-0.5` -> `-0.25`: `FAIL: facts_st.bin differs ...`; record payload byte `+7` -> `+8`: `FAIL: records_st.bin
  differs from the reference frames`; record CRC `+ 1`: `FAIL: bad CRC32 in the frame at 0`. All exit 1.
- Stress: the CRC of every 97th record off by one: `records 990 verified ... 10 failures`.
(Mutated copies were in the scratchpad and are deleted.)

## Results

- New tests: `ctest -R 'binfile_stress|cli_binary'`: 4 of 4 pass (cli_binary_lock, cli_binary_lock_protobase,
  cli_binary_interop, binfile_stress).
- Stress x10: 10 of 10.
- Full suite (`ctest --test-dir build_release -j1 --output-on-failure`): `100% tests passed, 0 tests failed out of 1260` (212 s; no skips).

## Addendum (controller ruling S12): in-process one-appender guard

- `src/primitives/binfile_prims.cpp`: `OpenFile` gained `std::atomic<bool> appending`; the RAII `AppendClaim` takes it by
  compare-exchange and releases it in its destructor, so every exit path (including exceptions) releases it. A second
  concurrent `append:` on the same BinaryFile signals Error "BinaryFile>>append: another append is in progress on this
  BinaryFile (one appender at a time)" and writes nothing. No mutex, no allocation under a lock; the claim sits inside
  the unmanaged bracket, before the held file reference, so it is released before a pending close drops the descriptor.
- `lib/kernel/binfile.st` class comment: states the in-process detection and that appenders in other processes, or on
  another BinaryFile object over the same file, cannot be detected.
- Test `tests/conformance/15-binary/binfile_append_guard.st`: 64 actors x 50 appends of 4 KiB CRC-framed records on one
  BinaryFile. Asserts: every result is a success or the named Error (0 other), successful offsets unique and tiling
  the file from 0 to its size, every record reads back with a valid CRC32. Collisions really happen (observed 114,
  144, 168 rejected of 3,200 per run); the number depends on scheduling and is not asserted. Runtime about 1.7 s.
- Mutation: with the claim commented out the test reports "38 bad records" (overlapping offsets / corrupt records);
  restored, it passes.
- Results: new test 10 of 10; `15-binary` + `cli_binary`: 96 of 96; full suite
  `100% tests passed, 0 tests failed out of 1261`. No temp files left in /tmp.
