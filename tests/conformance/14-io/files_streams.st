"EXPECT: 42 second 3 #(0 1 255 128)"
"File streams: writeStreamDo:, readStreamDo:, linesDo:, binary contents."
dir := FileSystem temp / ('protost-io-streams-' , Smalltalk pid printString).
dir ensureCreateDirectory.
f := dir / 'data.txt'.
f writeStreamDo: [:s | s print: 42; cr; nextPutAll: 'second'; cr; nextPutAll: 'third'].
first := nil. second := nil.
f readStreamDo: [:s | first := s nextLine. second := s nextLine].
count := 0.
f linesDo: [:line | count := count + 1].
b := dir / 'bytes.bin'.
b binaryContents: #(0 1 255 128).
bytes := b binaryContents.
dir deleteAll.
first , ' ' , second , ' ' , count printString , ' ' , bytes printString.
