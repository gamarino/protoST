"EXPECT-ERROR: 256 is not a byte"
"A byte WriteStream refuses what is not a byte."
(WriteStream on: ByteArray new) nextPut: 256.
