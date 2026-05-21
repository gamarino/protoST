"-- stream.st: the Stream standard-library module --

Provides ReadStream and WriteStream — cursor-based access over Track 2
collections. Pure protoST: no new primitives.

  ReadStream  — sequential reading over a fixed collection.
  WriteStream — sequential appending into a growable OrderedCollection.

Usage:
  m  := Import from: 'stream'.
  rs := m ReadStream on: #(1 2 3).
  rs next.            => 1
  ws := m WriteStream new.
  ws nextPut: 1; nextPut: 2.
  ws contents.        => an Array 1 2

The `<<` binary selector on WriteStream appends a whole collection (an alias
for nextPutAll:)."

"=== ReadStream ============================================================="

Object subclass: #ReadStream
  instanceVariableNames: 'collection position'.

"Class-side constructor: ReadStream on: aCollection."
ReadStream class >> on: aCollection
  | s |
  s := self new.
  s setCollection: aCollection.
  ^ s.

"Private initialiser used by the constructor."
ReadStream >> setCollection: aCollection
  collection := aCollection.
  position := 1.

"The whole underlying collection."
ReadStream >> contents
  ^ collection.

"Reset the cursor to the start."
ReadStream >> reset
  position := 1.

"True when the cursor is past the last element."
ReadStream >> atEnd
  ^ position > collection size.

"The next element without advancing; nil at end."
ReadStream >> peek
  self atEnd ifTrue: [ ^ nil ].
  ^ collection at: position.

"The next element, advancing the cursor. Signals an Error past the end."
ReadStream >> next
  | element |
  self atEnd ifTrue: [ ^ Error signal: 'ReadStream>>next: past end of stream' ].
  element := collection at: position.
  position := position + 1.
  ^ element.

"An Array of the next n elements (advancing). Signals an Error if fewer than
 n elements remain."
ReadStream >> next: n
  | result |
  result := OrderedCollection new.
  [ result size < n ] whileTrue: [ result add: self next ].
  ^ result asArray.

"The remaining elements as an Array (advancing to the end)."
ReadStream >> upToEnd
  | result |
  result := OrderedCollection new.
  [ position <= collection size ] whileTrue: [ result add: self next ].
  ^ result asArray.

"Evaluate aBlock for each remaining element (advancing to the end)."
ReadStream >> do: aBlock
  [ position <= collection size ] whileTrue: [ aBlock value: self next ].

"The number of elements not yet read."
ReadStream >> size
  ^ collection size - (position - 1).

"True when no elements remain to be read."
ReadStream >> isEmpty
  ^ self atEnd.

"=== WriteStream ============================================================"

Object subclass: #WriteStream
  instanceVariableNames: 'collection'.

"Class-side constructors. `WriteStream new` yields an empty, ready-to-use
 stream (the class-side override sends `initialize` itself, since the language
 does not auto-invoke it). `WriteStream on: aCollection` only seeds the
 species — content always starts empty, matching the Smalltalk contract."
WriteStream class >> new
  | s |
  s := super new.
  s initialize.
  ^ s.

WriteStream class >> on: aCollection
  ^ self new.

"Reset the backing store to an empty growable collection."
WriteStream >> initialize
  collection := OrderedCollection new.

"Append one element; return the element."
WriteStream >> nextPut: anElement
  collection add: anElement.
  ^ anElement.

"Append every element of aCollection; return the argument collection."
WriteStream >> nextPutAll: aCollection
  aCollection do: [ :each | collection add: each ].
  ^ aCollection.

"Binary alias for nextPutAll: — append a whole collection."
WriteStream >> << aCollection
  ^ self nextPutAll: aCollection.

"The accumulated elements as an Array."
WriteStream >> contents
  ^ collection asArray.

"The number of elements written so far."
WriteStream >> size
  ^ collection size.

"True when nothing has been written."
WriteStream >> isEmpty
  ^ collection isEmpty.
