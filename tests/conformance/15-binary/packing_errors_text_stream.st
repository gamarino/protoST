"EXPECT-ERROR: needs a byte stream"
"Packing needs a byte stream."
(WriteStream on: String new) nextPutUnsigned: 1 width: 1 bigEndian: true.
