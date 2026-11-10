"EXPECT-ERROR: an Integer was expected"
"A byte stream's integer field takes an Integer, not a Float or a String."
(WriteStream on: ByteArray new) nextPutUnsigned: 1.5 width: 4 bigEndian: true.
