"Kernel: printing. printString and displayString are built on printOn:, so a
 class that overrides printOn: prints the same way everywhere (printString,
 displayString, printNl, the REPL, a script's result). basicPrintString is the
 primitive 'a ClassName' form (digits for numbers, the name for a class)."

Object >> printOn: aStream
  aStream nextPutAll: self basicPrintString.

Object >> printString
  ^ String streamContents: [:s | self printOn: s].

Object >> displayString
  ^ self printString.

Object >> printNl
  self __stdout: self printString , (String with: Character lf).
  ^ self.

Object >> displayNl
  self __stdout: self displayString , (String with: Character lf).
  ^ self.

String >> printNl
  self __stdout: self printString , (String with: Character lf).
  ^ self.

String >> displayNl
  self __stdout: self , (String with: Character lf).
  ^ self.

"--- literals -------------------------------------------------------------"

String >> printOn: aStream
  "protoCore dispatches Symbol values to String; class tells them apart."
  self class == Symbol ifTrue: [^ aStream nextPut: $#; nextPutAll: self].
  aStream nextPut: $'.
  self do: [:c |
    c = $' ifTrue: [aStream nextPut: $'].
    aStream nextPut: c].
  aStream nextPut: $'.

Character >> printOn: aStream
  aStream nextPut: $$; nextPut: self.

Boolean >> printOn: aStream
  aStream nextPutAll: (self ifTrue: ['true'] ifFalse: ['false']).

UndefinedObject >> printOn: aStream
  aStream nextPutAll: 'nil'.

Number >> displayString
  ^ self printString.

Association >> printOn: aStream
  aStream print: self key; nextPutAll: '->'; print: self value.

Exception >> printOn: aStream
  aStream nextPutAll: self class name.
  self messageText = self class name ifFalse: [
    aStream nextPutAll: ': '; nextPutAll: self messageText displayString].

"--- collections ------------------------------------------------------------
 A collection prints its elements; printing stops ('...') past a nesting
 depth, so a collection that contains itself prints instead of recursing."

Collection >> printOn: aStream
  self __printEnter > 16 ifTrue: [
    self __printExit.
    ^ aStream nextPutAll: '...'].
  [aStream nextPutAll: self basicPrintString.
   self printElementsOn: aStream] ensure: [self __printExit].

Collection >> printElementsOn: aStream
  aStream nextPut: $(.
  self do: [:each | aStream print: each] separatedBy: [aStream space].
  aStream nextPut: $).

Array >> printOn: aStream
  self __printEnter > 16 ifTrue: [
    self __printExit.
    ^ aStream nextPutAll: '...'].
  [aStream nextPut: $#.
   self printElementsOn: aStream] ensure: [self __printExit].

Dictionary >> printElementsOn: aStream
  | first |
  first := true.
  aStream nextPut: $(.
  self keysAndValuesDo: [:k :v |
    first ifFalse: [aStream space].
    first := false.
    aStream print: k; nextPutAll: '->'; print: v].
  aStream nextPut: $).

Interval >> printOn: aStream
  "Print the expression that built the interval, as Pharo does; an empty
   interval prints too."
  aStream nextPut: $(; print: self intervalStart; nextPutAll: ' to: '; print: self intervalStop.
  self increment = 1 ifFalse: [aStream nextPutAll: ' by: '; print: self increment].
  aStream nextPut: $).
