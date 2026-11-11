"EXPECT: true true true true"
"An Array of Integers is not bytes: since 0.10.0 (D45) binaryContents:,
 nextPutBytes: and UDP send: refuse it with an Error that names ByteArray
 as the replacement, and write nothing (0.9.0 accepted it with a warning).
 A ByteArray made from the Array goes through."
| dir f named out u rx |
dir := FileSystem temp / ('protost-io-bytes-array-' , Smalltalk pid printString).
dir ensureCreateDirectory.
f := dir / 'b.bin'.
named := [:block | [block value. false] on: Error do: [:e |
  (e messageText includesSubstring: 'an Array is not bytes')
    and: [e messageText includesSubstring: 'ByteArray withAll:']]].
out := Array new: 4.
out at: 1 put: (named value: [f binaryContents: #(0 1 255)]).
out at: 2 put: f exists not.
out at: 3 put: (named value: [Stdio stdout nextPutBytes: #(65 10)]).
f binaryContents: (ByteArray withAll: #(0 1 255)).
out at: 4 put: f binaryContents = #[0 1 255].
dir deleteAll.
(out collect: [:x | x printString]) inject: '' into: [:acc :x |
  acc isEmpty ifTrue: [x] ifFalse: [acc , ' ' , x]].
