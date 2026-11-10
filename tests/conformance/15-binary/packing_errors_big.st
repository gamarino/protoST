"EXPECT-ERROR: does not fit"
"2^64 does not fit eight unsigned bytes."
(WriteStream on: ByteArray new) nextPutUnsigned: (1 bitShift: 64) width: 8 bigEndian: true.
