"-- uuid.st: the UUID value --

A UUID (RFC 4122): 16 bytes in RFC 4122 (network, big-endian) order. The
model is Python's uuid.UUID, the value of ProtoBase's @uuid (format 4), and
the meaning is Pharo's: an identifier, with UUID new a fresh random one.

USAGE
  m := Import from: 'uuid'.               (binds the global UUID too)
  u := UUID fromString: '12345678-1234-5678-1234-567812345678'.
  u printString.                          => '12345678-1234-5678-1234-567812345678'
  u hex.                                  => '12345678123456781234567812345678'
  u bytes.                                => the 16 bytes, a ByteArray
  u asInteger.                            => 24197857161011715162171839636988778104
  UUID new.                               => a new version-4 UUID

CONSTRUCTION
  fromBytes: takes a ByteArray of exactly 16 bytes. fromString: takes the 32
  hexadecimal digits or the 36-character form (8-4-4-4-12 digits separated by
  hyphens), in either case; nothing else (no braces, no urn:uuid: prefix, no
  surrounding whitespace). Anything else signals Error naming the argument.
  new answers a version-4 UUID (RFC 4122 section 4.4): 16 bytes from the
  operating system's secure generator (ByteArray randomBytes: 16), with the
  version nibble (the high four bits of byte 7) set to 4 and the variant (the
  two high bits of byte 9) set to 2r10.

TEXT
  printString (and displayString) is the 36-character lowercase form, which is
  Python's str(uuid); hex is the 32 lowercase digits, Python's uuid.hex.

COMPARISON
  = is true for another UUID with the same value, an instance of a subclass of
  UUID included (in either direction, as Python's isinstance test), and false
  for anything else (the bytes, the hex String or the Integer of the same UUID
  included), as in Python. hash is asInteger hash, so it agrees with = and equals Python's
  hash(uuid), which is hash(uuid.int). <, <=, >, >= order by asInteger (the
  unsigned big-endian value); ordering against a non-UUID signals Error.

The value is immutable: its bytes are a ByteArray, itself immutable, and the
only initialiser is private (_setBytes:), used by the class-side constructors,
and refuses a second call."

Object subclass: #UUID
  instanceVariableNames: 'bytes value'.

"=== class side: constructors ==============================================="

"The UUID of aByteArray, 16 bytes in RFC 4122 order."
UUID class >> fromBytes: aByteArray
  ((aByteArray isKindOf: ByteArray) and: [aByteArray size = 16]) ifFalse: [
    ^ Error signal: 'UUID fromBytes: a ByteArray of 16 bytes was expected, not '
      , aByteArray printString].
  ^ self basicNew _setBytes: aByteArray.

"The UUID of 32 hexadecimal digits or of the 36-character form, in either case."
UUID class >> fromString: aString
  | digits |
  (aString isString and: [aString isSymbol not]) ifFalse: [
    ^ Error signal: 'UUID fromString: a String was expected, not ' , aString printString].
  aString size = 32
    ifTrue: [digits := aString]
    ifFalse: [
      aString size = 36 ifFalse: [^ self _invalid: aString].
      #(9 14 19 24) do: [:i | (aString at: i) = $- ifFalse: [^ self _invalid: aString]].
      digits := (aString copyFrom: 1 to: 8) , (aString copyFrom: 10 to: 13)
        , (aString copyFrom: 15 to: 18) , (aString copyFrom: 20 to: 23)
        , (aString copyFrom: 25 to: 36)].
  1 to: 32 do: [:i | (self _isHexDigit: (digits at: i) asInteger) ifFalse: [^ self _invalid: aString]].
  ^ self basicNew _setBytes: (ByteArray fromHex: digits).

"A new version-4 UUID from the operating system's secure random bytes."
UUID class >> new
  | random |
  random := ByteArray randomBytes: 16.
  ^ self basicNew _setBytes: (random
      copyReplaceFrom: 7 to: 9
      with: { ((random at: 7) bitAnd: 16r0F) bitOr: 16r40.
              random at: 8.
              ((random at: 9) bitAnd: 16r3F) bitOr: 16r80 }).

UUID class >> _isHexDigit: aCode
  ^ (aCode between: 48 and: 57) or: [(aCode between: 65 and: 70) or: [aCode between: 97 and: 102]].

UUID class >> _invalid: aString
  ^ Error signal: 'UUID fromString: not 32 hexadecimal digits nor the 36-character form: '
    , aString printString.

"=== private initialiser ===================================================="

UUID >> _setBytes: aByteArray
  "Runs once, on a fresh instance: a live UUID never changes."
  bytes isNil ifFalse: [^ Error signal: 'UUID is immutable: ' , self printString].
  bytes := aByteArray.
  value := aByteArray inject: 0 into: [:acc :each | acc * 256 + each].
  ^ self.

"=== accessors =============================================================="

"The 16 bytes in RFC 4122 order (an immutable ByteArray)."
UUID >> bytes
  ^ bytes.

"The unsigned big-endian value, 0 to 2^128 - 1 (Python's uuid.int)."
UUID >> asInteger
  ^ value.

"The 32 lowercase hexadecimal digits (Python's uuid.hex)."
UUID >> hex
  ^ bytes hex.

"=== printing ==============================================================="

"The 36-character lowercase form (Python's str(uuid))."
UUID >> printOn: aStream
  | h |
  h := bytes hex.
  aStream
    nextPutAll: (h copyFrom: 1 to: 8); nextPut: $-;
    nextPutAll: (h copyFrom: 9 to: 12); nextPut: $-;
    nextPutAll: (h copyFrom: 13 to: 16); nextPut: $-;
    nextPutAll: (h copyFrom: 17 to: 20); nextPut: $-;
    nextPutAll: (h copyFrom: 21 to: 32).

"=== comparison ============================================================="

UUID >> = other
  ^ (other isKindOf: UUID) and: [value = other asInteger].

UUID >> hash
  ^ value hash.

UUID >> < other
  ^ value < (self _orderKey: other).

UUID >> <= other
  ^ value <= (self _orderKey: other).

UUID >> > other
  ^ value > (self _orderKey: other).

UUID >> >= other
  ^ value >= (self _orderKey: other).

UUID >> _orderKey: other
  (other isKindOf: UUID) ifFalse: [
    ^ Error signal: 'UUID: cannot order ' , self printString , ' against ' , other printString].
  ^ other asInteger.
