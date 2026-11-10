"EXPECT: 60000 true true 1 true"
"A document of many small objects, round-tripped several times so the collector runs while the decoder holds unfinished containers: 20000 records (a map of a string, a float and a byte string), decoded as Dictionaries and, ordered, as MessagePackMaps whose re-encoding is byte-identical. Nesting 500 deep (inside the limit) round-trips without a stack overflow."
| doc bytes back ordered deep |
doc := (1 to: 20000) collect: [:i |
  MessagePackMap withAll: {'id' -> i. 'name' -> ('record-', i printString). 'raw' -> (ByteArray new: 3). 'f' -> (i / 7.0)}].
bytes := MessagePack encode: doc.
3 timesRepeat: [back := MessagePack decode: bytes].
ordered := MessagePack decodeOrdered: bytes.
deep := Array new.
499 timesRepeat: [deep := Array with: deep].
(back size * 3) printString, ' ',
  (((back at: 77) at: 'name') = 'record-77') printString, ' ',
  ((MessagePack encode: ordered) = bytes) printString, ' ',
  (MessagePack decode: (MessagePack encode: deep)) size printString, ' ',
  (((MessagePack decode: (MessagePack encode: deep)) = deep)) printString
