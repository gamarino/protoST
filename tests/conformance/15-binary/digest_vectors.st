"EXPECT: crc32 0 failed of 16, combine 0 failed of 11, blake2b 0 failed of 112, sha256+hmac 0 failed of 42"
"Every vector of protoIO's crc32.json, blake2b.json and sha256_hmac.json (made by
 CPython's zlib, hashlib and hmac), so that a digest computed here is the digest
 ProtoBase computes over the same bytes. Includes the 1 MiB crc input."
| crc blake sha failed count report |
crc := Import from: '_vectors_crc32'.
blake := Import from: '_vectors_blake2b'.
sha := Import from: '_vectors_sha256_hmac'.
report := WriteStream on: String new.

failed := 0. count := 0.
crc VectorsCrc32 all do: [:e |
  count := count + 1.
  ((e at: 1) crc32Continuing: (e at: 2)) = (e at: 3) ifFalse: [failed := failed + 1]].
report nextPutAll: 'crc32 ', failed printString, ' failed of ', count printString.

failed := 0. count := 0.
crc VectorsCrc32 combine do: [:e |
  count := count + 1.
  (CRC32 combine: (e at: 3) with: (e at: 4) length: (e at: 5)) = (e at: 6)
    ifFalse: [failed := failed + 1].
  "The combined value is also what crc32 of the concatenation gives."
  ((e at: 1) , (e at: 2)) crc32 = (e at: 6) ifFalse: [failed := failed + 1]].
report nextPutAll: ', combine ', failed printString, ' failed of ', count printString.

failed := 0. count := 0.
blake VectorsBlake2b all do: [:e |
  | d |
  count := count + 1.
  d := (e at: 2) isEmpty
    ifTrue: [(e at: 1) blake2b: (e at: 3)]
    ifFalse: [(e at: 1) blake2b: (e at: 3) key: (e at: 2)].
  (d = (e at: 4) and: [(e at: 2) isEmpty not or: [
     ((e at: 1) blake2b: (e at: 3) key: (e at: 2)) = (e at: 4)]])
    ifFalse: [failed := failed + 1]].
report nextPutAll: ', blake2b ', failed printString, ' failed of ', count printString.

failed := 0. count := 0.
sha VectorsSha256Hmac all do: [:e |
  count := count + 1.
  (e at: 1) = 'sha256'
    ifTrue: [(e at: 2) sha256 = (e at: 3) ifFalse: [failed := failed + 1]]
    ifFalse: [(HMAC sha256Key: (e at: 2) message: (e at: 3)) = (e at: 4)
                ifFalse: [failed := failed + 1]]].
report nextPutAll: ', sha256+hmac ', failed printString, ' failed of ', count printString.
report contents.
