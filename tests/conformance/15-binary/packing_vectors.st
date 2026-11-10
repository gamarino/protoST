"EXPECT: 0 failed of 168 checked"
"Every entry of protoIO's packing.json (made by CPython's struct), in both
 directions: writing the value gives the stored bytes, reading the stored
 bytes gives the value. Floats are compared by bit pattern, so a NaN payload
 and -0.0 count: the entry's value is the pattern as big-endian hex, a float
 is read from it, and what is written is compared as bytes."
lib := Import from: '_vectors_packing'.
failed := 0.
checked := 0.
lib VectorsPacking all do: [:e |
  | kind width big value bytes ok s read pattern |
  kind := e at: 1. width := e at: 2. big := e at: 3. value := e at: 4. bytes := e at: 5.
  checked := checked + 1.
  s := WriteStream on: ByteArray new.
  (kind = 'u' or: [kind = 'i'])
    ifTrue: [
      kind = 'u'
        ifTrue: [s nextPutUnsigned: value width: width bigEndian: big]
        ifFalse: [s nextPutSigned: value width: width bigEndian: big].
      read := (kind = 'u')
        ifTrue: [width = 1 ifTrue: [bytes unsignedByteAt: 1] ifFalse: [
          width = 2 ifTrue: [bytes unsignedShortAt: 1 bigEndian: big] ifFalse: [
          width = 4 ifTrue: [bytes unsignedLongAt: 1 bigEndian: big]
                    ifFalse: [bytes unsignedLong64At: 1 bigEndian: big]]]]
        ifFalse: [width = 1 ifTrue: [bytes signedByteAt: 1] ifFalse: [
          width = 2 ifTrue: [bytes signedShortAt: 1 bigEndian: big] ifFalse: [
          width = 4 ifTrue: [bytes signedLongAt: 1 bigEndian: big]
                    ifFalse: [bytes signedLong64At: 1 bigEndian: big]]]].
      ok := (s contents = bytes) and: [read = value]]
    ifFalse: [
      pattern := ByteArray fromHex: value.
      kind = 'f32'
        ifTrue: [
          read := bytes float32At: 1 bigEndian: big.
          s nextPutFloat32: (pattern float32At: 1 bigEndian: true) bigEndian: big]
        ifFalse: [
          read := bytes float64At: 1 bigEndian: big.
          s nextPutFloat64: (pattern float64At: 1 bigEndian: true) bigEndian: big].
      "The float read from the stored bytes, written big-endian, gives the pattern."
      ok := (s contents = bytes) and: [
        | t |
        t := WriteStream on: ByteArray new.
        kind = 'f32'
          ifTrue: [t nextPutFloat32: read bigEndian: true]
          ifFalse: [t nextPutFloat64: read bigEndian: true].
        t contents = pattern]].
  ok ifFalse: [failed := failed + 1]].
failed printString, ' failed of ', checked printString, ' checked'.
