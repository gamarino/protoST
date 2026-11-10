"Kernel: ByteArray, an immutable sequence of bytes.

 A ByteArray is a value, like a String (D34): integers from 0 to 255, NUL
 included, held in protoCore's ProtoByteBuffer and never changed once made.
 Operations that would change one answer a new ByteArray instead, so a
 ByteArray can be shared between actors and used as a Dictionary key without
 a lock; at:put: signals ModificationForbidden. = and hash go by contents.

 The literal is Pharo's: #[1 2 255]. Two deliberate differences from Pharo,
 recorded in docs/STATUS.md: a ByteArray cannot be changed in place, and it
 has no asString (Pharo's is Latin-1; text goes through UTF-8 explicitly).
 The primitives are in src/primitives/bytes_prims.cpp; the design is section
 4.1 of docs/superpowers/specs/2026-11-09-bytes-files-timers-design.md."

"--- instance creation ---------------------------------------------------------"

ByteArray class >> new
  "The empty ByteArray."
  ^ self __bytesNew: 0.

ByteArray class >> new: anInteger
  "anInteger zero bytes."
  ^ self __bytesNew: anInteger.

ByteArray class >> withAll: aCollection
  "The bytes of aCollection, whose elements are Integers from 0 to 255; any
   other element signals Error. A ByteArray is answered as it is: it is a value."
  aCollection class == ByteArray ifTrue: [^ aCollection].
  ^ self __bytesWithAll: aCollection asArray.

ByteArray class >> fromHex: aString
  "The bytes aString spells in hexadecimal, two digits per byte, in either case."
  ^ self __fromHex: aString.

"--- accessing ------------------------------------------------------------------"

ByteArray >> size
  ^ self __bytesSize.

ByteArray >> at: anIndex
  "The byte at anIndex, an Integer from 0 to 255."
  ^ self __bytesAt: anIndex.

ByteArray >> isEmpty
  ^ self size = 0.

ByteArray >> notEmpty
  ^ self size > 0.

ByteArray >> at: anIndex put: aByte
  ^ ModificationForbidden new signal: 'ByteArrays are immutable in protoST: at:put: cannot change a ByteArray. Build a new one with , (concatenation), copyReplaceFrom:to:with: or ByteArray withAll:'.

"--- iteration ------------------------------------------------------------------"

ByteArray >> do: aBlock
  1 to: self size do: [:i | aBlock value: (self at: i)].

ByteArray >> collect: aBlock
  "An Array of the results: a block may answer something that is not a byte."
  ^ self asArray collect: aBlock.

ByteArray >> inject: initial into: aBlock
  | acc |
  acc := initial.
  self do: [:b | acc := aBlock value: acc value: b].
  ^ acc.

ByteArray >> asArray
  "The bytes as an Array of Integers."
  | out |
  out := Array new: self size.
  1 to: self size do: [:i | out at: i put: (self at: i)].
  ^ out.

"--- copying: every one answers a new value -------------------------------------"

ByteArray >> copyFrom: start to: stop
  ^ self __bytesCopyFrom: start to: stop.

ByteArray >> , aByteArray
  ^ self __bytesConcat: aByteArray.

ByteArray >> copyReplaceFrom: start to: stop with: replacement
  "The receiver with the bytes from start to stop replaced by the bytes of
   replacement (a ByteArray, or a collection of Integers from 0 to 255). With
   stop = start - 1 the bytes are inserted before start."
  ^ (self copyFrom: 1 to: start - 1) , (ByteArray withAll: replacement)
      , (self copyFrom: stop + 1 to: self size).

"--- comparing ------------------------------------------------------------------"

ByteArray >> = other
  ^ self __bytesEquals: other.

ByteArray >> hash
  "FNV-1a over the contents: equal ByteArrays hash alike, in every run."
  ^ self __bytesHash.

"--- converting and printing ----------------------------------------------------"

ByteArray >> hex
  "Lowercase hexadecimal, two digits per byte."
  ^ self __bytesHex.

ByteArray >> printOn: aStream
  aStream nextPutAll: '#['.
  1 to: self size do: [:i |
    i > 1 ifTrue: [aStream nextPut: Character space].
    aStream print: (self at: i)].
  aStream nextPut: $].
