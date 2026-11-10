"EXPECT: 0 e3b0c442 3421780262 true 64 20 true"
"Spot values that need no vector file: the crc of nothing, the first four bytes of the SHA-256
 of nothing, the standard CRC-32 check value ('123456789' = 0xCBF43926), an empty key equal to
 no key, a 64-byte key (the longest), digest sizes, and equal inputs giving equal digests."
#[] crc32 printString, ' ',
  (#[] sha256 copyFrom: 1 to: 4) hex, ' ',
  '123456789' utf8Encoded crc32 printString, ' ',
  ((#[1 2] blake2b: 20 key: #[]) = (#[1 2] blake2b: 20)) printString, ' ',
  (#[1 2] blake2b: 64 key: (ByteArray new: 64)) size printString, ' ',
  (#[1 2] blake2b: 20) size printString, ' ',
  ((#[1 2] blake2b: 64) = (#[1 2] blake2b: 64)) printString.
