"EXPECT-ERROR: does not fit"
"1e39 is beyond single precision."
(WriteStream on: ByteArray new) nextPutFloat32: 1e39 bigEndian: true.
