"EXPECT-ERROR: "
"An unsigned field refuses a negative value."
(WriteStream on: ByteArray new) nextPutUnsigned: -1 width: 4 bigEndian: true.
