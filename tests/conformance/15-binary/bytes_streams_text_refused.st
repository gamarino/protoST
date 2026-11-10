"EXPECT-ERROR: utf8Encoded"
"Text goes into a byte stream through utf8Encoded, never implicitly."
(WriteStream on: ByteArray new) nextPutAll: 'abc'.
