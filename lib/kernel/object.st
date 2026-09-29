"Kernel: Object protocol written in protoST itself.

 Reflection that needs the runtime (class, superclass, respondsTo:,
 perform:withArguments:, shallowCopy, hash, identityHash) is primitive; the
 rest of the everyday protocol is defined here in terms of it."

Object >> yourself
  ^ self.

Object >> value
  ^ self.

"--- class membership -------------------------------------------------------"

Object >> isKindOf: aClass
  | c |
  c := self class.
  [c notNil] whileTrue: [
    c == aClass ifTrue: [^ true].
    c := c superclass].
  ^ false.

Object >> isMemberOf: aClass
  ^ self class == aClass.

Object >> isString
  ^ false.

Object >> isSymbol
  ^ false.

Object >> isNumber
  ^ false.

Object >> isInteger
  ^ false.

Object >> isFloat
  ^ false.

Object >> isCollection
  ^ false.

Object >> isBlock
  ^ false.

Object >> isCharacter
  ^ false.

"--- sending messages by name ------------------------------------------------"

Object >> perform: aSelector
  ^ self perform: aSelector withArguments: #().

Object >> perform: aSelector with: anObject
  ^ self perform: aSelector withArguments: (Array with: anObject).

Object >> perform: aSelector with: first with: second
  ^ self perform: aSelector withArguments: (Array with: first with: second).

Object >> perform: aSelector with: first with: second with: third
  ^ self perform: aSelector withArguments: (Array with: first with: second with: third).

"--- errors -------------------------------------------------------------------"

Object >> error: aString
  ^ Error new signal: aString.

Object >> subclassResponsibility
  ^ self error: 'This method is a subclass responsibility'.

Object >> shouldNotImplement
  ^ self error: 'This message is not appropriate for this object'.

"--- copying ------------------------------------------------------------------"

Object >> copy
  ^ self shallowCopy postCopy.

Object >> postCopy
  ^ self.

"--- testing kinds of objects -------------------------------------------------"

Number >> isNumber
  ^ true.

Integer >> isInteger
  ^ true.

Float >> isFloat
  ^ true.

String >> isString
  ^ true.

Collection >> isCollection
  ^ true.
