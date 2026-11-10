"Kernel: WriteStream and ReadStream over Strings, ByteArrays and sequenceable
 collections, available without an import. On a String they read and build
 Strings; on a ByteArray they read and build ByteArrays (bytes are Integers
 from 0 to 255); on other collections they read elements and build an Array.

 A byte WriteStream (WriteStream on: ByteArray new) keeps its bytes in
 private state: pending single bytes in an OrderedCollection and written
 ByteArrays in a list of chunks. contents joins them into a new ByteArray each
 time, so a ByteArray it answered is never touched by later writes."

Object subclass: #WriteStream instanceVariableNames: 'buffer onString onBytes chunks'.

WriteStream class >> new
  ^ self basicNew setOn: #().

WriteStream class >> on: aCollection
  ^ self basicNew setOn: aCollection.

WriteStream class >> with: aCollection
  ^ (self basicNew setOn: aCollection) nextPutAll: aCollection; yourself.

WriteStream >> setOn: aCollection
  onString := aCollection isString.
  onBytes := aCollection class == ByteArray.
  onBytes ifTrue: [chunks := OrderedCollection new].
  buffer := onString ifTrue: [''] ifFalse: [OrderedCollection new].
  ^ self.

WriteStream >> nextPut: anElement
  onBytes ifTrue: [^ self putByte: anElement].
  onString
    ifTrue: [buffer := buffer , anElement asString]
    ifFalse: [buffer add: anElement].
  ^ anElement.

WriteStream >> nextPutAll: aCollection
  onBytes ifTrue: [^ self putBytes: aCollection].
  (onString and: [aCollection isString])
    ifTrue: [buffer := buffer , aCollection]
    ifFalse: [aCollection do: [:each | self nextPut: each]].
  ^ aCollection.

WriteStream >> << anObject
  onBytes ifTrue: [
    (anObject isInteger) ifTrue: [self nextPut: anObject] ifFalse: [self nextPutAll: anObject].
    ^ self].
  onString
    ifTrue: [self nextPutAll: (anObject isString ifTrue: [anObject] ifFalse: [anObject displayString])]
    ifFalse: [anObject isCollection
      ifTrue: [self nextPutAll: anObject]
      ifFalse: [self nextPut: anObject]].
  ^ self.

WriteStream >> print: anObject
  self nextPutAll: anObject printString.
  ^ self.

WriteStream >> display: anObject
  self nextPutAll: anObject displayString.
  ^ self.

WriteStream >> space
  self nextPut: Character space.
  ^ self.

WriteStream >> tab
  self nextPut: Character tab.
  ^ self.

WriteStream >> cr
  self nextPut: Character lf.
  ^ self.

WriteStream >> lf
  self nextPut: Character lf.
  ^ self.

WriteStream >> contents
  "A String, a new ByteArray (on bytes) or an Array of what was written."
  onBytes ifTrue: [^ self bytesContents].
  ^ onString ifTrue: [buffer] ifFalse: [buffer asArray].

WriteStream >> size
  onBytes ifTrue: [^ chunks inject: buffer size into: [:sum :each | sum + each size]].
  ^ buffer size.

WriteStream >> isEmpty
  ^ self size = 0.

"--- bytes ------------------------------------------------------------------"

WriteStream >> putByte: anInteger
  (anInteger isInteger and: [anInteger between: 0 and: 255]) ifFalse: [
    ^ Error new signal: anInteger printString , ' is not a byte: a byte stream takes Integers from 0 to 255 (text goes in through utf8Encoded)'].
  buffer add: anInteger.
  buffer size >= 4096 ifTrue: [self flushBytes].
  ^ anInteger.

WriteStream >> putBytes: aCollection
  "Append a ByteArray, or an Array of Integers from 0 to 255; anything else
   (a String, nil, another collection) signals Error."
  aCollection class == ByteArray ifTrue: [
    self flushBytes.
    chunks add: aCollection.
    ^ aCollection].
  aCollection class == Array ifFalse: [
    ^ Error new signal: 'A byte stream takes a ByteArray or an Array of bytes, not ' , aCollection printString , ' (text goes in through utf8Encoded)'].
  aCollection do: [:each | self putByte: each].
  ^ aCollection.

WriteStream >> nextPutUnsigned: anInteger width: aWidth bigEndian: aBoolean
  "Append anInteger as aWidth (1, 2, 4 or 8) bytes; a negative value, or one
   that does not fit, signals Error. Byte streams only."
  onBytes ifFalse: [^ Error new signal: 'nextPutUnsigned:width:bigEndian: needs a byte stream (WriteStream on: ByteArray new)'].
  self putBytes: (ByteArray __packUnsigned: anInteger width: aWidth bigEndian: aBoolean).
  ^ anInteger.

WriteStream >> nextPutSigned: anInteger width: aWidth bigEndian: aBoolean
  "Append anInteger in two's complement as aWidth bytes; a value that does
   not fit signals Error. Byte streams only."
  onBytes ifFalse: [^ Error new signal: 'nextPutSigned:width:bigEndian: needs a byte stream (WriteStream on: ByteArray new)'].
  self putBytes: (ByteArray __packSigned: anInteger width: aWidth bigEndian: aBoolean).
  ^ anInteger.

WriteStream >> nextPutFloat32: aNumber bigEndian: aBoolean
  "Append aNumber rounded to IEEE 754 single precision (4 bytes)."
  onBytes ifFalse: [^ Error new signal: 'nextPutFloat32:bigEndian: needs a byte stream (WriteStream on: ByteArray new)'].
  self putBytes: (ByteArray __packFloat: aNumber width: 4 bigEndian: aBoolean).
  ^ aNumber.

WriteStream >> nextPutFloat64: aNumber bigEndian: aBoolean
  "Append aNumber as IEEE 754 double precision (8 bytes)."
  onBytes ifFalse: [^ Error new signal: 'nextPutFloat64:bigEndian: needs a byte stream (WriteStream on: ByteArray new)'].
  self putBytes: (ByteArray __packFloat: aNumber width: 8 bigEndian: aBoolean).
  ^ aNumber.

WriteStream >> flushBytes
  "Move the pending single bytes into a chunk, keeping the order."
  buffer isEmpty ifTrue: [^ self].
  chunks add: (ByteArray withAll: buffer).
  buffer := OrderedCollection new.

WriteStream >> bytesContents
  self flushBytes.
  ^ ByteArray __bytesJoin: chunks asArray.

Object subclass: #ReadStream instanceVariableNames: 'collection position'.

ReadStream class >> on: aCollection
  ^ self basicNew setOn: aCollection.

ReadStream >> setOn: aCollection
  collection := aCollection.
  position := 0.
  ^ self.

ReadStream >> atEnd
  ^ position >= collection size.

ReadStream >> next
  "Past the end this answers nil, as in Pharo; atEnd tells the two apart."
  self atEnd ifTrue: [^ nil].
  position := position + 1.
  ^ collection at: position.

"next:, upTo: and upToEnd answer a copy of part of the collection, so the
 result has the collection's kind: a String from a String, an
 OrderedCollection from an OrderedCollection, an Array from an Array."

ReadStream >> next: n
  "Answer the next n elements, or fewer if the stream ends first."
  | stop result |
  stop := (position + n) min: collection size.
  result := collection copyFrom: position + 1 to: stop.
  position := stop.
  ^ result.

ReadStream >> peek
  self atEnd ifTrue: [^ nil].
  ^ collection at: position + 1.

ReadStream >> peekFor: anObject
  "Consume the next element and answer true if it equals anObject; otherwise
   leave the stream where it is and answer false."
  (self atEnd not and: [self peek = anObject]) ifFalse: [^ false].
  position := position + 1.
  ^ true.

ReadStream >> skip: n
  position := (position + n) max: 0.
  position := position min: collection size.
  ^ self.

ReadStream >> skipSeparators
  [self atEnd not and: [self peek isSeparator]] whileTrue: [position := position + 1].

ReadStream >> skipTo: anObject
  "Advance past the next occurrence of anObject; answer whether it was found."
  [self atEnd] whileFalse: [self next = anObject ifTrue: [^ true]].
  ^ false.

ReadStream >> upTo: anObject
  "Answer the elements up to, not including, anObject (or to the end), and
   advance past anObject."
  | start |
  start := position.
  [self atEnd] whileFalse: [
    self next = anObject ifTrue: [^ collection copyFrom: start + 1 to: position - 1]].
  ^ collection copyFrom: start + 1 to: position.

ReadStream >> upToEnd
  ^ self next: collection size - position.

ReadStream >> do: aBlock
  [self atEnd] whileFalse: [aBlock value: self next].

ReadStream >> position
  ^ position.

ReadStream >> position: anInteger
  position := anInteger.
  ^ self.

ReadStream >> reset
  position := 0.
  ^ self.

ReadStream >> contents
  ^ collection.

ReadStream >> size
  ^ collection size.

ReadStream >> isEmpty
  ^ collection size = 0.

String class >> streamContents: aBlock
  | s |
  s := WriteStream on: ''.
  aBlock value: s.
  ^ s contents.

String class >> new: size streamContents: aBlock
  ^ self streamContents: aBlock.
