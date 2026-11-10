"EXPECT-ERROR: does not fit"
"-2^63-1 does not fit signed eight bytes."
(WriteStream on: ByteArray new) nextPutSigned: (-9223372036854775808 - 1) width: 8 bigEndian: true.
