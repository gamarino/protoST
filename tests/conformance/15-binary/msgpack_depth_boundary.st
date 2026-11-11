"EXPECT: depth 1024 round-trips; 1025 refused on encode and decode"
"protoIO's MessagePack depth limit (msgpack-python 1.2's unpacker limit, and the record depth
 ProtoBase format 4 requires every reader to accept): exactly 1024 nested containers encode and
 decode; 1025 signal Error both ways."
| deep a bytes back n encodeRefused decodeRefused |
deep := [:levels | | v | v := Array new. (levels - 1) timesRepeat: [v := Array with: v]. v].
a := deep value: 1024.
bytes := MessagePack encode: a.
bytes size = 1024 ifFalse: [Error signal: 'expected 1024 bytes, got ' , bytes size printString].
back := MessagePack decode: bytes.
n := 1.
[back isEmpty] whileFalse: [back := back first. n := n + 1].
n = 1024 ifFalse: [Error signal: 'decoded depth ' , n printString].
encodeRefused := [MessagePack encode: (deep value: 1025). false] on: Error do: [:e | e return: true].
encodeRefused ifFalse: [Error signal: 'encode of depth 1025 was accepted'].
decodeRefused := [MessagePack decode: (ByteArray new: 1025 withAll: 16r91) , (ByteArray with: 16r90). false]
  on: Error do: [:e | e return: true].
decodeRefused ifFalse: [Error signal: 'decode of depth 1026 was accepted'].
decodeRefused := [MessagePack decode: (ByteArray new: 1024 withAll: 16r91) , (ByteArray with: 16r90). false]
  on: Error do: [:e | e return: true].
decodeRefused ifFalse: [Error signal: 'decode of depth 1025 was accepted'].
'depth 1024 round-trips; 1025 refused on encode and decode'
