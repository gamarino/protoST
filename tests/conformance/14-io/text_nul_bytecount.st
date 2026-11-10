"EXPECT: 5 5"
"nextByteCount: answers text (not a ByteArray) and keeps a NUL inside the bytes read."
nul := String with: (Character value: 0).
dir := FileSystem temp / ('protost-io-nulb-' , Smalltalk pid printString).
dir ensureCreateDirectory.
f := dir / 't.txt'.
f contents: 'ab' , nul , 'cd'.
got := nil.
f readStreamDo: [:s | got := s nextByteCount: 5].
dir deleteAll.
got size printString, ' ', (got class == String ifTrue: [got size] ifFalse: [0]) printString
