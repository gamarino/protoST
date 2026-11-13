"EXPECT: true true true true true true true true true true true"
"§13 stdlib: UUID comparison (lib/uuid.st), as Python's uuid: = compares the
 values, whatever text built them, and is false against anything that is not
 a UUID (its bytes, its hex String, its Integer); hash is the hash of
 asInteger, so it agrees with = and with Python's hash(uuid), which is
 hash(uuid.int) (276701140489800253 for the value below); < and the other
 orderings compare asInteger, and ordering against a non-UUID signals Error.
 Equal UUIDs are one Set element and one Dictionary key. An instance of a
 subclass of UUID equals a UUID of the same value, in both directions, as
 Python's isinstance test makes it."
Import from: 'uuid'.
UUID subclass: #TaggedUUID.
| m U a b low high set dict bad |
m := Import from: 'uuid'.
U := m UUID.
a := U fromString: '12345678-1234-5678-1234-567812345678'.
b := U fromString: '12345678123456781234567812345678'.
low := U fromString: '00000000000000000000000000000001'.
high := U fromString: 'ff000000000000000000000000000000'.
set := Set new. set add: a; add: b; add: low.
dict := Dictionary new. dict at: a put: 1. dict at: b put: 2.
bad := [:block | [block value. false] on: Error do: [:e | e return: true]].
{ a = b and: [b = a and: [a hash = b hash]].
  (a = low) not and: [a ~= low].
  (a = a bytes) not & (a = a hex) not & (a = a asInteger) not.
  a hash = a asInteger hash.
  a hash = 276701140489800253.
  low < high and: [(high < low) not and: [(a < b) not]].
  high > low & (low <= low) & (high >= low) & (a <= b).
  (bad value: [low < 1]) & (bad value: [low > 'x']).
  set size = 2.
  dict size = 1 and: [(dict at: a) = 2].
  (TaggedUUID fromString: a hex) = a and: [a = (TaggedUUID fromString: a hex) and: [(TaggedUUID fromString: a hex) hash = a hash]] }
  inject: '' into: [:acc :each | acc isEmpty ifTrue: [each printString] ifFalse: [acc , ' ' , each printString]].
