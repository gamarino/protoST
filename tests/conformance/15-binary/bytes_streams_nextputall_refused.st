"EXPECT-ERROR: a byte stream takes a ByteArray, not nil"
(WriteStream on: ByteArray new) nextPutAll: nil.
