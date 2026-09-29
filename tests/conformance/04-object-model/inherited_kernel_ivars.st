"EXPECT: 2 abc"
"A subclass of a kernel class reads the instance variables it inherits
 (IOStream's fd, WriteStream's buffer), as a Pharo subclass would."
IOStream subclass: #LabelledStream instanceVariableNames: 'label'.
LabelledStream >> descriptorPlusOne
  ^ fd + 1

WriteStream subclass: #PeekStream.
PeekStream >> peekBuffer
  ^ buffer

s := PeekStream on: String new.
s nextPutAll: 'abc'.
((LabelledStream onDescriptor: 1) descriptorPlusOne) printString , ' ' , s peekBuffer.
