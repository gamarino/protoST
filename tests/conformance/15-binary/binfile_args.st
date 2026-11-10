"EXPECT: true true true true true true true"
"BinaryFile: wrong arguments signal Error with a message that names the problem."
dir := FileSystem temp / ('protost-binfile-args-' , Smalltalk pid printString).
dir ensureCreateDirectory.
[| f msg checks |
 f := BinaryFile open: (dir / 'seg') pathString options: #(#read #write #create).
 msg := [:block | [block value. 'no error'] on: Error do: [:e | e messageText]].
 checks := {
   (msg value: [f append: 'text']) includesSubstring: 'a ByteArray was expected'.
   (msg value: [f writeAt: 0 bytes: #(1 2)]) includesSubstring: 'a ByteArray was expected'.
   (msg value: [f readAt: -1 size: 1]) includesSubstring: 'negative'.
   (msg value: [f readAt: 0 size: -1]) includesSubstring: 'must not be negative'.
   (msg value: [f readAt: 'x' size: 1]) includesSubstring: 'an Integer was expected'.
   (msg value: [BinaryFile open: (dir / 'other') pathString options: #(#create)]) includesSubstring: 'neither read nor write'.
   (msg value: [BinaryFile open: (dir / 'other') pathString options: 'read']) includesSubstring: 'an Array of Symbols was expected'}.
 f close.
 (checks collect: [:each | each printString]) inject: '' into: [:acc :each | acc isEmpty ifTrue: [each] ifFalse: [acc , ' ' , each]]]
  ensure: [dir deleteAll].
