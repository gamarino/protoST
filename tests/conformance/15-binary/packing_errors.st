"EXPECT-ERROR: does not fit"
"A value that does not fit the width signals Error; it never wraps."
(WriteStream on: ByteArray new) nextPutUnsigned: 256 width: 1 bigEndian: false.
