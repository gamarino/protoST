"EXPECT: 16 0 true true"
"ByteArray randomBytes: n answers n bytes from the operating system's
 cryptographically secure generator (protoIO random::bytes), as a frozen
 ByteArray like every other: at:put: signals ModificationForbidden. Two
 32-byte draws are equal with probability 2^-256, so they differ."
| a b frozen |
a := ByteArray randomBytes: 32.
b := ByteArray randomBytes: 32.
frozen := [(ByteArray randomBytes: 4) at: 1 put: 0. false]
  on: ModificationForbidden do: [:e | true].
(ByteArray randomBytes: 16) size printString , ' ' ,
  (ByteArray randomBytes: 0) size printString , ' ' ,
  (a = b) not printString , ' ' , frozen printString.
