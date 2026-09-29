"Kernel: WriteStream and ReadStream over Strings and sequenceable
 collections, available without an import. On a String they read and build
 Strings; on other collections they read elements and build an Array."

Object subclass: #WriteStream instanceVariableNames: 'buffer onString'.

WriteStream class >> new
  ^ self basicNew setOn: #().

WriteStream class >> on: aCollection
  ^ self basicNew setOn: aCollection.

WriteStream class >> with: aCollection
  ^ (self basicNew setOn: aCollection) nextPutAll: aCollection; yourself.

WriteStream >> setOn: aCollection
  onString := aCollection isString.
  buffer := onString ifTrue: [''] ifFalse: [OrderedCollection new].
  ^ self.

WriteStream >> nextPut: anElement
  onString
    ifTrue: [buffer := buffer , anElement asString]
    ifFalse: [buffer add: anElement].
  ^ anElement.

WriteStream >> nextPutAll: aCollection
  (onString and: [aCollection isString])
    ifTrue: [buffer := buffer , aCollection]
    ifFalse: [aCollection do: [:each | self nextPut: each]].
  ^ aCollection.

WriteStream >> << anObject
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
  ^ onString ifTrue: [buffer] ifFalse: [buffer asArray].

WriteStream >> size
  ^ buffer size.

WriteStream >> isEmpty
  ^ buffer size = 0.

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
