"EXPECT: #[127 255 255 255 255 255 255 255] 9223372036854775807 #[128 0 0 0 0 0 0 0] -9223372036854775808 #[255 255 255 255 255 255 255 255] -1"
"The 64-bit edges: the largest signed, the smallest signed, and all bits set read as signed and unsigned."
a := WriteStream on: ByteArray new. a nextPutSigned: 9223372036854775807 width: 8 bigEndian: true.
b := WriteStream on: ByteArray new. b nextPutSigned: -9223372036854775808 width: 8 bigEndian: true.
c := #[255 255 255 255 255 255 255 255].
a contents printString, ' ', (a contents signedLong64At: 1 bigEndian: true) printString, ' ',
  b contents printString, ' ', (b contents signedLong64At: 1 bigEndian: true) printString, ' ',
  c printString, ' ', (c signedLong64At: 1 bigEndian: true) printString.
