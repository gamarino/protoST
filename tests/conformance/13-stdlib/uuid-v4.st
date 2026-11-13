"EXPECT: true true true true true"
"§13 stdlib: UUID new is a version-4 UUID (RFC 4122 section 4.4) built from
 ByteArray randomBytes: 16 (protoST 0.10.0): the high nibble of byte 7 is 4
 and the two high bits of byte 9 are 2r10, on each of 200 new UUIDs; the
 200 are pairwise different (a collision has probability below 2^-106); a
 new UUID parses back from its printString. A binary without randomBytes:
 skips visibly."
| m U u all |
(ByteArray respondsTo: #randomBytes:) ifFalse: [^ 'SKIP: needs ByteArray randomBytes: (protoST 0.10.0)'].
m := Import from: 'uuid'.
U := m UUID.
u := U new.
all := (1 to: 200) asArray collect: [:i | U new].
{ ((u bytes at: 7) bitShift: -4) = 4.
  ((u bytes at: 9) bitShift: -6) = 2r10.
  all allSatisfy: [:each | ((each bytes at: 7) bitShift: -4) = 4 and: [((each bytes at: 9) bitShift: -6) = 2r10]].
  (all asSet size = 200) & (U new ~= U new).
  (U fromString: u printString) = u }
  inject: '' into: [:acc :each | acc isEmpty ifTrue: [each printString] ifFalse: [acc , ' ' , each printString]].
