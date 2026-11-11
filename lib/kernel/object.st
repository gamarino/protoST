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
  "Whether the receiver is an instance of aClass or of a class that has
   aClass among its ancestors, superclasses and mixins (`uses:`,
   addBehavior:) alike: a mixin is a parent class in protoST (LANGUAGE.md
   §4.11), the same ancestor set `on: aClass do:` selects an exception by."
  | c |
  c := self class.
  ^ c == aClass or: [c __hasAncestor: aClass].

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

Object >> isClass
  ^ self isClassObject.

Object >> asString
  ^ self printString.

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

"--- instance creation ---------------------------------------------------------
 new sends initialize, as in Smalltalk-80 implementations since Pharo/Squeak;
 basicNew is the raw allocator."

Object class >> new
  | instance |
  instance := self basicNew.
  instance initialize.
  ^ instance.

Object >> initialize
  ^ self.

"--- class-instance variables -----------------------------------------------------
 `Foo class instanceVariableNames: 'default'` declares slots of the class
 object itself; the compiler reads the declaration, and at run time the
 metaclass records the names, so `Foo class instVarNames` lists them, as in
 Pharo."

Behavior >> instanceVariableNames: aString
  self __setInstVarNames: aString.
  ^ self.
