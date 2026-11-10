"EXPECT: #[0 1 2 0] true #[0 1 255 128] #[1 2] true"
"binaryContents: takes a ByteArray; binaryContents and nextBytes: answer one."
dir := FileSystem temp / ('protost-io-bytes-' , Smalltalk pid printString).
dir ensureCreateDirectory.
f := dir / 'b.bin'.
f binaryContents: #[0 1 2 0].
back := f binaryContents.
f binaryContents: #[0 1 255 128].
second := f binaryContents.
f binaryContents: #[1 2].
head := nil.
f readStreamDo: [:s | head := s nextBytes: 10].
dir deleteAll.
back printString, ' ', (back = #[0 1 2 0]) printString, ' ', second printString, ' ',
  head printString, ' ', (head class == ByteArray) printString.
