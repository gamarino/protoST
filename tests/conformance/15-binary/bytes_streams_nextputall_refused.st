"EXPECT-ERROR: takes a ByteArray or an Array of bytes"
(WriteStream on: ByteArray new) nextPutAll: nil.
