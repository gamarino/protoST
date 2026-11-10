"EXPECT-ERROR: does not fit"
"An Integer beyond the Float range is refused, not written as infinity."
(WriteStream on: ByteArray new) nextPutFloat64: (10 raisedTo: 400) bigEndian: true.
