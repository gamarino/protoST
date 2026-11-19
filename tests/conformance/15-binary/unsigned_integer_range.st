"EXPECT: #(0 0 255 255 72057594037927935 18446744073709551615 4722366482869645213695 340282366920938463463374607431768211455 258 513 197121 SmallInteger SmallInteger LargeInteger LargeInteger 9007199254740991 9007199254740992 18446744073709551616 1 true true true true) 0 mismatches"
"ByteArray>>unsignedIntegerFrom:to:bigEndian: answers the unsigned Integer
 that the bytes from start to stop spell, big-endian or little-endian: any
 number of bytes, the high bit never a sign. An empty range (stop =
 start - 1) answers 0. A value below 2^53 is a SmallInteger, anything above
 a LargeInteger. The randomized part checks every length from 0 to 40, both
 byte orders and sub-ranges, against the per-byte reference."
Object subclass: #BytesCheck instanceVariableNames: 'seed'.

BytesCheck >> next: bound
  seed := (seed * 6364136223846793005 + 1442695040888963407) \\ (2 raisedTo: 64).
  ^ (seed bitShift: -20) \\ bound.

BytesCheck >> reference: bytes from: start to: stop bigEndian: big
  | acc |
  acc := 0.
  big
    ifTrue: [start to: stop do: [:i | acc := (acc bitShift: 8) + (bytes at: i)]]
    ifFalse: [stop to: start by: -1 do: [:i | acc := (acc bitShift: 8) + (bytes at: i)]].
  ^ acc.

BytesCheck >> run
  | bad bytes start stop big |
  seed := 20261119.
  bad := 0.
  0 to: 40 do: [:len |
    20 timesRepeat: [
      bytes := ByteArray withAll: ((1 to: len + 6) collect: [:i |
        (self next: 3) = 0 ifTrue: [255] ifFalse: [self next: 256]]).
      start := (self next: 4) + 1.
      stop := start + len - 1.
      big := (self next: 2) = 0.
      (bytes unsignedIntegerFrom: start to: stop bigEndian: big)
        = (self reference: bytes from: start to: stop bigEndian: big)
        ifFalse: [bad := bad + 1.
          (bytes printString , ' ' , start printString , ' ' , stop printString) displayNl]]].
  ^ bad.

| ff oid r |
ff := [:n | ByteArray withAll: ((1 to: n) collect: [:i | 255])].
oid := ByteArray withAll: (1 to: 16).
r := {
  ByteArray new unsignedIntegerFrom: 1 to: 0 bigEndian: true.
  #[1 2 3] unsignedIntegerFrom: 3 to: 2 bigEndian: false.
  #[255] unsignedIntegerFrom: 1 to: 1 bigEndian: true.
  #[255] unsignedIntegerFrom: 1 to: 1 bigEndian: false.
  (ff value: 7) unsignedIntegerFrom: 1 to: 7 bigEndian: true.
  (ff value: 8) unsignedIntegerFrom: 1 to: 8 bigEndian: false.
  (ff value: 9) unsignedIntegerFrom: 1 to: 9 bigEndian: true.
  (ff value: 16) unsignedIntegerFrom: 1 to: 16 bigEndian: false.
  #[1 2] unsignedIntegerFrom: 1 to: 2 bigEndian: true.
  #[1 2] unsignedIntegerFrom: 1 to: 2 bigEndian: false.
  #[9 1 2 3 9] unsignedIntegerFrom: 2 to: 4 bigEndian: false.
  (#[31 255 255 255 255 255 255] unsignedIntegerFrom: 1 to: 7 bigEndian: true) class.
  (#[255 255 255 255 255 255 31] unsignedIntegerFrom: 1 to: 7 bigEndian: false) class.
  (#[32 0 0 0 0 0 0] unsignedIntegerFrom: 1 to: 7 bigEndian: true) class.
  ((ff value: 16) unsignedIntegerFrom: 1 to: 16 bigEndian: true) class.
  #[31 255 255 255 255 255 255] unsignedIntegerFrom: 1 to: 7 bigEndian: true.
  #[0 0 0 0 0 0 32] unsignedIntegerFrom: 1 to: 7 bigEndian: false.
  #[1 0 0 0 0 0 0 0 0] unsignedIntegerFrom: 1 to: 9 bigEndian: true.
  #[1 0 0 0 0 0 0 0 0] unsignedIntegerFrom: 1 to: 9 bigEndian: false.
  (oid unsignedIntegerFrom: 1 to: 16 bigEndian: true)
    = (((oid unsignedLong64At: 1 bigEndian: true) bitShift: 64) + (oid unsignedLong64At: 9 bigEndian: true)).
  (oid unsignedIntegerFrom: 1 to: 16 bigEndian: false)
    = ((ByteArray withAll: (16 to: 1 by: -1)) unsignedIntegerFrom: 1 to: 16 bigEndian: true).
  (oid unsignedIntegerFrom: 1 to: 16 bigEndian: true) = 16r0102030405060708090A0B0C0D0E0F10.
  ((oid unsignedIntegerFrom: 9 to: 16 bigEndian: true) = (oid unsignedLong64At: 9 bigEndian: true)) }.
r printString , ' ' , BytesCheck new run printString , ' mismatches'.
