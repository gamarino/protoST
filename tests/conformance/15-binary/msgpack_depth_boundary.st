"EXPECT: depth 1024 round-trips; 1025 refused on encode and decode"
"protoIO's MessagePack depth limit (msgpack-python 1.2's unpacker limit, and the record depth
 ProtoBase format 4 requires every reader to accept): exactly 1024 nested containers encode and
 decode; 1025 signal MessagePackDepthExceeded (an Error) both ways, and a malformed document
 signals an Error that is not one."
| deep fixarrays a bytes back n encodeRefused decodeRefused |
deep := [:levels | | v | v := Array new. (levels - 1) timesRepeat: [v := Array with: v]. v].
fixarrays := [:k | | hex | hex := ''. k timesRepeat: [hex := hex , '91']. ByteArray fromHex: hex , '90'].
a := deep value: 1024.
bytes := MessagePack encode: a.
bytes = (fixarrays value: 1023) ifFalse: [Error signal: 'encoded bytes differ'].
bytes size = 1024 ifFalse: [Error signal: 'expected 1024 bytes, got ' , bytes size printString].
back := MessagePack decode: bytes.
n := 1.
[back isEmpty] whileFalse: [back := back first. n := n + 1].
n = 1024 ifFalse: [Error signal: 'decoded depth ' , n printString].
encodeRefused := [MessagePack encode: (deep value: 1025). false] on: MessagePackDepthExceeded do: [:e | e return: true].
encodeRefused ifFalse: [Error signal: 'encode of depth 1025 was accepted'].
decodeRefused := [MessagePack decode: (fixarrays value: 1025). false]
  on: MessagePackDepthExceeded do: [:e | e return: true].
decodeRefused ifFalse: [Error signal: 'decode of depth 1026 was accepted'].
decodeRefused := [MessagePack decode: (fixarrays value: 1024). false]
  on: MessagePackDepthExceeded do: [:e | e return: true].
decodeRefused ifFalse: [Error signal: 'decode of depth 1025 was accepted'].
(MessagePackDepthExceeded inheritsFrom: Error)
  ifFalse: [Error signal: 'MessagePackDepthExceeded is not an Error'].
([MessagePack decode: (ByteArray with: 16r91). false]
    on: Error do: [:e | e return: (e isKindOf: MessagePackDepthExceeded)])
  ifTrue: [Error signal: 'a truncated document signalled MessagePackDepthExceeded'].
'depth 1024 round-trips; 1025 refused on encode and decode'
