"EXPECT: true true true true true true true"
"An Array of Integers is not bytes: since 0.10.0 (D45) every write that takes
 bytes or a String refuses it with an Error that names the selector the
 program sent and ByteArray withAll: as the replacement, and writes nothing
 (0.9.0 accepted it with a warning): binaryContents:, contents:,
 appendContents:, nextPutBytes: and nextPutAll: (UDP send: is in
 net_udp_array_refused.st). A ByteArray made from the Array goes through."
| dir f refusedAs out |
dir := FileSystem temp / ('protost-io-bytes-array-' , Smalltalk pid printString).
dir ensureCreateDirectory.
f := dir / 'b.bin'.
refusedAs := [:selector :block | [block value. false] on: Error do: [:e |
  ((e messageText beginsWith: selector , ' an Array is not bytes')
    and: [e messageText includesSubstring: 'ByteArray withAll:'])]].
out := OrderedCollection new.
out add: (refusedAs value: 'binaryContents:' value: [f binaryContents: #(0 1 255)]).
out add: (refusedAs value: 'contents:' value: [f contents: #(0 1 255)]).
out add: (refusedAs value: 'appendContents:' value: [f appendContents: #(0 1 255)]).
out add: f exists not.
out add: (refusedAs value: 'nextPutBytes:' value: [Stdio stdout nextPutBytes: #(65 10)]).
out add: (refusedAs value: 'nextPutAll:' value: [Stdio stdout nextPutAll: #(65 10)]).
f binaryContents: (ByteArray withAll: #(0 1 255)).
out add: f binaryContents = #[0 1 255].
dir deleteAll.
(out collect: [:x | x printString]) inject: '' into: [:acc :x |
  acc isEmpty ifTrue: [x] ifFalse: [acc , ' ' , x]].
