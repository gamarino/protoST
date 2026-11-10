"EXPECT: 2 2 1 2 true"
"A map with a repeated key ({'k': 1, 'k': 2}): the last entry wins, whichever way it is decoded (Dictionary, MessagePackMap at:ifAbsent:) and as msgpack-python's unpackb does; the ordered form still holds both entries. Equal MessagePackMaps hash alike."
| bytes d m m2 |
bytes := #[16r82 16rA1 16r6B 1 16rA1 16r6B 2].
d := MessagePack decode: bytes.
m := MessagePack decodeOrdered: bytes.
m2 := MessagePack decodeOrdered: bytes.
(d at: 'k') printString, ' ', (m at: 'k' ifAbsent: [nil]) printString, ' ',
  d size printString, ' ', m size printString, ' ',
  ((m = m2) and: [m hash = m2 hash]) printString
