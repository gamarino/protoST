"Kernel: Object protocol written in protoST itself.

 Reflection that needs the runtime (class, superclass, respondsTo:,
 perform:withArguments:, shallowCopy, hash, identityHash) is primitive; the
 rest of the everyday protocol is defined here in terms of it."

Object >> yourself
  ^ self.

Object >> value
  ^ self.

"--- class membership -------------------------------------------------------"

"isKindOf: aClass is a primitive (object_prims.cpp): whether the receiver is
 an instance of aClass or of a class that has aClass among its ancestors,
 superclasses and mixins (`uses:`, addBehavior:) alike: a mixin is a parent
 class in protoST (LANGUAGE.md §4.11), the same ancestor set `on: aClass do:`
 selects an exception by."

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

"perform:, perform:with:, perform:with:with: and perform:with:with:with: are
 primitives (0.10.0), like perform:withArguments:: they no longer build an
 Array of the arguments."

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
 basicNew is the raw allocator. For a class with constants, new then checks
 that initialize set every one (explicit-state spec §6.2, M13); basicNew does
 not."

Object class >> new
  | instance |
  instance := self basicNew.
  instance initialize.
  self __hasConstants ifTrue: [^ self __checkConstantsOf: instance after: #new].
  ^ instance.

"The end of a kernel class-side constructor that does not reach Object
 class >> new (SortedCollection new and sortBlock:, WriteStream on:, Random
 seed:, Point x:y:, ...): the constructor makes anInstance with basicNew and
 its kernel setter, then this sends initialize once, as Object class >> new
 does, and runs the end-of-construction check, naming aSelector (M13, spec
 §6.2, which makes no exception for kernel constructors). Answers anInstance,
 or nothing when a constant of the receiver is unset. The kernel classes that
 use it define no initialize of their own, so for them the send runs
 Object >> initialize and changes nothing, and their setup runs once; a user
 subclass's initialize runs after that setup, and may set its constants."
Object class >> __checkedNew: anInstance after: aSelector
  anInstance initialize.
  self __hasConstants ifTrue: [^ self __checkConstantsOf: anInstance after: aSelector].
  ^ anInstance.

"The same, for a class-side new."
Object class >> __checkedNew: anInstance
  ^ self __checkedNew: anInstance after: #new.

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
