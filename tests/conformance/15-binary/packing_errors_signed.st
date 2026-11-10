"EXPECT-ERROR: does not fit"
"A signed field refuses 128 in one byte."
(WriteStream on: ByteArray new) nextPutSigned: 128 width: 1 bigEndian: true.
