#!/usr/bin/env bash
# Byte-exact round trip with CPython (docs/superpowers/specs/
# 2026-11-09-bytes-files-timers-design.md, sections 4.7 and 4.8).
#
# Usage: test_cli_binary_interop.sh <protost> <python> <interop_peer.py>
#
# <python> is the interpreter CMake found (Python3_EXECUTABLE), or `none`.
#
# 1. CPython writes data.msgpack (msgpack.packb(use_bin_type=True), a nested
#    value with every type), facts_py.bin (struct, crc32, blake2b, sha256) and
#    records_py.bin (CRC32-framed records).
# 2. protoST reads them: decodeOrdered: then encode: must give back the same
#    bytes (back.msgpack); the numbers and digests must match; every record
#    must verify.
# 3. protoST writes the same kinds of files (out.msgpack from a
#    MessagePackMap, facts_st.bin, records_st.bin) and CPython verifies them,
#    comparing bytes, not only decoded values.
#
# Without Python or msgpack it prints a SKIP line and exits 77, which CTest
# reports as skipped.
set -euo pipefail
PROTOST="$1"
PYTHON="$2"
PEER="$3"
if [[ -z "$PYTHON" || "$PYTHON" == none ]] || ! "$PYTHON" -c "import sys" > /dev/null 2>&1; then
    echo "SKIP: no usable Python 3 interpreter from CMake (${PYTHON:-none given}), CPython interop NOT verified"
    exit 77
fi
if ! "$PYTHON" -c "import msgpack" > /dev/null 2>&1; then
    echo "SKIP: the Python msgpack module is missing, CPython interop NOT verified"
    exit 77
fi
dir=$(mktemp -d "${TMPDIR:-/tmp}/protost-interop-XXXXXX")
trap 'rm -rf "$dir"' EXIT

cat > "$dir/side.st" <<'ST'
| dir fails read facts frames data back sample out w p |
dir := Smalltalk arguments at: 1.
fails := OrderedCollection new.
read := [:name | (dir , '/' , name) asFileReference binaryContents].
data := read value: 'data.msgpack'.

"A. the msgpack document written by CPython: same bytes after decodeOrdered: + encode:"
back := MessagePack encode: (MessagePack decodeOrdered: data).
(dir , '/back.msgpack') asFileReference binaryContents: back.
back = data ifFalse: [fails add: 'decodeOrdered: + encode: changed the bytes'].

"B. numbers and digests written by CPython"
facts := read value: 'facts_py.bin'.
((facts unsignedByteAt: 1) = 200 and: [(facts unsignedShortAt: 2 bigEndian: true) = 51234
  and: [(facts unsignedLongAt: 4 bigEndian: true) = 4000000000
  and: [(facts unsignedLong64At: 8 bigEndian: true) = ((2 raisedTo: 63) + 5)]]])
  ifFalse: [fails add: 'big-endian unsigned fields'].
((facts signedByteAt: 16) = -100 and: [(facts signedShortAt: 17 bigEndian: false) = -30000
  and: [(facts signedLongAt: 19 bigEndian: false) = -2000000000
  and: [(facts signedLong64At: 23 bigEndian: false) = ((2 raisedTo: 62) negated)
  and: [(facts float32At: 31 bigEndian: false) = 1.5
  and: [(facts float64At: 35 bigEndian: false) = -2.25e100]]]]])
  ifFalse: [fails add: 'little-endian signed and float fields'].
((facts signedLong64At: 43 bigEndian: true) = ((2 raisedTo: 63) negated)
  and: [(facts float32At: 51 bigEndian: true) = -0.5
  and: [(facts float64At: 55 bigEndian: true) = 2.5e-300]])
  ifFalse: [fails add: 'big-endian signed and float fields'].
(facts unsignedLongAt: 63 bigEndian: true) = data crc32 ifFalse: [fails add: 'crc32'].
(facts copyFrom: 67 to: 86) = (data blake2b: 20) ifFalse: [fails add: 'blake2b 20'].
(facts copyFrom: 87 to: 118) = (data blake2b: 32 key: (ByteArray fromHex: '6b6b6b6b6b6b6b6b6b6b6b6b6b6b6b6b'))
  ifFalse: [fails add: 'keyed blake2b'].
(facts copyFrom: 119 to: 150) = data sha256 ifFalse: [fails add: 'sha256'].
facts size = 150 ifFalse: [fails add: 'facts size ' , facts size printString].

"C. framed records written by CPython"
frames := BinaryFile open: dir , '/records_py.bin' options: #(#read).
p := 0.
0 to: 49 do: [:i | | header len crc payload |
  header := frames readAt: p size: 8.
  header size = 8 ifFalse: [fails add: 'record ' , i printString , ': short header'].
  len := header unsignedLongAt: 1 bigEndian: false.
  crc := header unsignedLongAt: 5 bigEndian: false.
  payload := frames readAt: p + 8 size: len.
  (len = (i * 6) and: [crc = payload crc32 and: [payload size = len]])
    ifFalse: [fails add: 'record ' , i printString].
  p := p + 8 + len].
p = frames size ifFalse: [fails add: 'trailing bytes after the records'].
frames close.

"D. protoST writes a document with the same content, in the same key order"
sample := MessagePackMap withAll: {
  'z_nil' -> nil. 'a_true' -> true. 'm_false' -> false.
  'ints' -> {0. 1. 127. 128. 255. 256. 65535. 65536. (2 raisedTo: 32) - 1. 2 raisedTo: 32.
    (2 raisedTo: 63) - 1. 2 raisedTo: 63. (2 raisedTo: 64) - 1.
    -1. -32. -33. -128. -129. -32768. -32769. (2 raisedTo: 31) negated. (2 raisedTo: 31) negated - 1.
    (2 raisedTo: 63) negated}.
  'floats' -> {1.5. -0.0. 3.141592653589793. 1.0e300}.
  'text' -> 'añ€ plain'.
  'bin' -> #[0 255 16].
  'empty_bin' -> #[].
  'ext' -> (MessagePackExt type: 5 data: #[120 121 122]).
  'nested' -> (MessagePackMap withAll: {
    'k2' -> {1. {2. {3. MessagePackMap withAll: {}}}}.
    'k1' -> (MessagePackMap withAll: {'deep' -> {}})}).
  '' -> 'empty key'}.
(dir , '/out.msgpack') asFileReference binaryContents: (MessagePack encode: sample).

"E. numbers and digests written by protoST"
w := WriteStream on: ByteArray new.
w nextPutUnsigned: 200 width: 1 bigEndian: true.
w nextPutUnsigned: 51234 width: 2 bigEndian: true.
w nextPutUnsigned: 4000000000 width: 4 bigEndian: true.
w nextPutUnsigned: (2 raisedTo: 63) + 5 width: 8 bigEndian: true.
w nextPutSigned: -100 width: 1 bigEndian: false.
w nextPutSigned: -30000 width: 2 bigEndian: false.
w nextPutSigned: -2000000000 width: 4 bigEndian: false.
w nextPutSigned: (2 raisedTo: 62) negated width: 8 bigEndian: false.
w nextPutFloat32: 1.5 bigEndian: false.
w nextPutFloat64: -2.25e100 bigEndian: false.
w nextPutSigned: (2 raisedTo: 63) negated width: 8 bigEndian: true.
w nextPutFloat32: -0.5 bigEndian: true.
w nextPutFloat64: 2.5e-300 bigEndian: true.
w nextPutUnsigned: data crc32 width: 4 bigEndian: true.
w nextPutAll: (data blake2b: 20).
w nextPutAll: (data blake2b: 32 key: (ByteArray fromHex: '6b6b6b6b6b6b6b6b6b6b6b6b6b6b6b6b')).
w nextPutAll: data sha256.
(dir , '/facts_st.bin') asFileReference binaryContents: w contents.

"F. records written by protoST through a BinaryFile"
out := BinaryFile open: dir , '/records_st.bin' options: #(#write #create #exclusive).
0 to: 49 do: [:i | | pw fw |
  pw := WriteStream on: ByteArray new.
  0 to: i * 6 - 1 do: [:j | pw nextPut: (i * j + 7) \\ 256].
  fw := WriteStream on: ByteArray new.
  fw nextPutUnsigned: pw contents size width: 4 bigEndian: false.
  fw nextPutUnsigned: pw contents crc32 width: 4 bigEndian: false.
  fw nextPutAll: pw contents.
  out append: fw contents].
out sync.
out close.

fails isEmpty
  ifTrue: [Transcript showCr: 'ST-OK']
  ifFalse: [fails do: [:each | Transcript showCr: 'FAIL: ' , each]. Smalltalk exit: 1].
ST

"$PYTHON" "$PEER" write "$dir"
"$PROTOST" "$dir/side.st" "$dir" > "$dir/side.out" 2>&1 || { echo "FAIL: protoST side failed:"; cat "$dir/side.out"; exit 1; }
grep -qx 'ST-OK' "$dir/side.out" || { echo "FAIL: protoST side:"; cat "$dir/side.out"; exit 1; }
"$PYTHON" "$PEER" check "$dir" > "$dir/check.out" 2>&1 || { cat "$dir/check.out"; exit 1; }
grep -qx 'PY-OK' "$dir/check.out" || { echo "FAIL: the CPython check did not report PY-OK:"; cat "$dir/check.out"; exit 1; }
echo "PASS: CPython and protoST exchange msgpack, packed numbers, digests and CRC-framed records byte for byte"
