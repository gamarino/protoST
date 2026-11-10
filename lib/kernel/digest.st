"Kernel: CRC32 and HMAC, the class-side forms of the digests.

 Both are namespaces of one-shot functions, not objects with state: there is
 no update: and nothing mutable, so nothing aliases an answered value. The
 instance-side forms are on ByteArray (crc32, sha256, blake2b:, ...). The
 functions are protoIO's, byte-compatible with CPython's zlib and hmac."

Object subclass: #CRC32 instanceVariableNames: ''.
Object subclass: #HMAC instanceVariableNames: ''.

CRC32 class >> combine: crcA with: crcB length: lengthOfB
  "The CRC-32 of A , B from the crc of A, the crc of B and the size in bytes of B
   (zlib's crc32_combine); with a length of 0 it is crcA. Both crcs are
   Integers from 0 to 2^32 - 1, the length a non-negative Integer."
  ^ ByteArray __crc32Combine: crcA with: crcB length: lengthOfB.

HMAC class >> sha256Key: aKey message: aMessage
  "The 32-byte HMAC-SHA-256 of aMessage under aKey (both ByteArrays), as a new ByteArray."
  ^ ByteArray __hmacSha256Key: aKey message: aMessage.
