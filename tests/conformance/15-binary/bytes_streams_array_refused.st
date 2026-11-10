"EXPECT-ERROR: a byte stream takes a ByteArray, not #(1 2 3)"
"A byte stream's nextPutAll: takes a ByteArray only; an Array of bytes goes
 in through ByteArray withAll:, the one rule for binary data."
(WriteStream on: ByteArray new) nextPutAll: #(1 2 3).
