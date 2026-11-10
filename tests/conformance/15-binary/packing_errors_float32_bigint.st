"EXPECT-ERROR: does not fit"
"The same for a single precision field."
(WriteStream on: ByteArray new) nextPutFloat32: (10 raisedTo: 400) bigEndian: true.
