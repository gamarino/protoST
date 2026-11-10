"EXPECT-ERROR: a byte stream takes a ByteArray, not #(4 5)"
"<< on a byte stream appends an Integer (one byte) or a ByteArray;
 an Array of bytes is refused like nextPutAll: refuses it."
(WriteStream on: ByteArray new) << 1 << #(4 5).
