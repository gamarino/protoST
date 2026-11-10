"EXPECT: #[1 2 0 0 0 0 3 63 240 0 0 0 0 0 0] 15 2 3 1.0"
"Fields written one after the other, and read back at their 1-based indices."
s := WriteStream on: ByteArray new.
s nextPut: 1; nextPutUnsigned: 2 width: 2 bigEndian: false; nextPutUnsigned: 3 width: 4 bigEndian: true;
  nextPutFloat64: 1.0 bigEndian: true.
b := s contents.
b printString, ' ', b size printString, ' ', (b unsignedShortAt: 2 bigEndian: false) printString, ' ',
  (b unsignedLongAt: 4 bigEndian: true) printString, ' ', (b float64At: 8 bigEndian: true) printString.
