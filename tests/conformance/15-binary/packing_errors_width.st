"EXPECT-ERROR: the width must be"
"Only widths 1, 2, 4 and 8 exist."
(WriteStream on: ByteArray new) nextPutUnsigned: 1 width: 3 bigEndian: true.
