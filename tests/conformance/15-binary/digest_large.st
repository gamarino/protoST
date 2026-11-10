"EXPECT: 3 of 3 inputs agree with CPython"
"Digests, CRC-32, HMAC and base64 just below and at the 64 KiB threshold from
 which they run outside the collector's quorum (bytes_prims.cpp, ruling S16),
 and above it, give CPython's results (hashlib, zlib, hmac, base64; the
 expected values were computed with CPython 3)."
| inputs expected agree |
inputs := {
  ByteArray new: 65535.
  ByteArray new: 65536.
  ByteArray withAll: ((1 to: 70000) collect: [:i | i \\ 251]) }.
expected := #(
  #(65535 '9f797b60edaf440d' 2503374279 'a820366b435312e7' '15dd22aadc90f2b7' '4484a7b9128e7780')
  #(65536 'de2f256064a0af79' 3617033963 'b73e5300e2ba4d5f' '272fa0ed9b1866d6' 'bbf8fda7bd2f7733')
  #(70000 '5451ad268d8852b9' 2768049113 '5209717089af2fb9' 'f243cbfce2aee546' '515c716ca4cb9d9a')).
agree := 0.
inputs with: expected do: [:data :want | | got text |
  text := data base64Encoded.
  got := {
    data size.
    data sha256 hex copyFrom: 1 to: 16.
    data crc32.
    (data blake2b: 8) hex.
    (HMAC sha256Key: #[1 2 3] message: data) hex copyFrom: 1 to: 16.
    text utf8Encoded sha256 hex copyFrom: 1 to: 16 }.
  (got = want and: [text base64Decoded = data])
    ifTrue: [agree := agree + 1]
    ifFalse: [('mismatch: ' , got printString) displayNl]].
(agree printString , ' of 3 inputs agree with CPython') displayNl.
