"EXPECT: #[255 255 255 255 255 255 255 255] #[128 0 0 0 0 0 0 0] 18446744073709551615 -9223372036854775808 #[0 128 0 0 0 0 0 0] 36028797018963968"
"Eight-byte fields go beyond the SmallInteger range (2^55): the values are LargeIntegers, in both directions."
a := WriteStream on: ByteArray new.
a nextPutUnsigned: 18446744073709551615 width: 8 bigEndian: true.
b := WriteStream on: ByteArray new.
b nextPutSigned: -9223372036854775808 width: 8 bigEndian: true.
c := WriteStream on: ByteArray new.
c nextPutUnsigned: 36028797018963968 width: 8 bigEndian: true.
a contents printString, ' ', b contents printString, ' ',
  (a contents unsignedLong64At: 1 bigEndian: true) printString, ' ',
  (b contents signedLong64At: 1 bigEndian: true) printString, ' ',
  c contents printString, ' ', (c contents unsignedLong64At: 1 bigEndian: true) printString.
