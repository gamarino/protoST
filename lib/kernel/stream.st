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
  self atEnd ifTrue: [^ self error: 'ReadStream>>next: past end of stream'].
  position := position + 1.
  ^ collection at: position.

ReadStream >> next: n
  | out |
  out := WriteStream on: (collection isString ifTrue: [''] ifFalse: [#()]).
  1 to: n do: [:i | self atEnd ifFalse: [out nextPut: self next]].
  ^ out contents.

ReadStream >> peek
  self atEnd ifTrue: [^ nil].
  ^ collection at: position + 1.

ReadStream >> skip: n
  position := (position + n) max: 0.
  position := position min: collection size.
  ^ self.

ReadStream >> upTo: anObject
  | out element |
  out := WriteStream on: (collection isString ifTrue: [''] ifFalse: [#()]).
  [self atEnd] whileFalse: [
    element := self next.
    element = anObject ifTrue: [^ out contents].
    out nextPut: element].
  ^ out contents.

ReadStream >> upToEnd
  | out |
  out := WriteStream on: (collection isString ifTrue: [''] ifFalse: [#()]).
  [self atEnd] whileFalse: [out nextPut: self next].
  ^ out contents.

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
