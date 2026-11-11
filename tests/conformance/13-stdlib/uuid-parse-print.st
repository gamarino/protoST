"EXPECT: true true true true true true true true true true true true true true"
"§13 stdlib: UUID text and bytes (lib/uuid.st). The 36-character form and its
 32-hex form parse to the same value, in either case; printString is the
 lowercase 36-character form and hex the 32 lowercase digits; fromBytes:
 keeps the 16 bytes in RFC 4122 order; asInteger is the unsigned big-endian
 value (Python's uuid.int: 24197857161011715162171839636988778104 for the
 text below, and 2^128 - 1 for ff..ff). Text of another length, a hyphen
 out of place, a character that is not a hexadecimal digit, a non-String,
 and bytes that are not a ByteArray of 16 signal Error."
| m U text hex u v w bad |
m := Import from: 'uuid'.
U := m UUID.
text := '12345678-1234-5678-1234-567812345678'.
hex := '12345678123456781234567812345678'.
u := U fromString: text.
v := U fromString: hex.
w := U fromString: 'ABCDEF00-ABCD-ABCD-ABCD-ABCDEF000000'.
bad := [:block | [block value. false] on: Error do: [:e | e return: true]].
{ u printString = text.
  u hex = hex.
  v printString = text.
  (U fromBytes: u bytes) printString = text.
  u bytes = (ByteArray fromHex: hex).
  u asInteger = 24197857161011715162171839636988778104.
  (U fromString: 'ffffffff-ffff-ffff-ffff-ffffffffffff') asInteger = ((2 raisedTo: 128) - 1).
  w printString = 'abcdef00-abcd-abcd-abcd-abcdef000000'.
  (U fromString: '00000000000000000000000000000001') asInteger = 1.
  (bad value: [U fromString: '1234567812345678123456781234567']) & (bad value: [U fromString: hex , '0']).
  (bad value: [U fromString: '123456781-234-5678-1234-567812345678']) & (bad value: [U fromString: '12345678-1234-5678-1234-56781234567x']).
  (bad value: [U fromString: '1234567g123456781234567812345678']) & (bad value: [U fromString: 42]).
  (bad value: [U fromBytes: (ByteArray new: 15)]) & (bad value: [U fromBytes: (ByteArray new: 17)]).
  (bad value: [U fromBytes: (1 to: 16) asArray]) & (bad value: [U fromBytes: hex]) }
  inject: '' into: [:acc :each | acc isEmpty ifTrue: [each printString] ifFalse: [acc , ' ' , each printString]].
