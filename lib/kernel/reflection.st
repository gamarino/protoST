"Kernel: reflection. Instance-variable access by name or index, the
 instance-variable and subclass lists of a class, class comments and
 categories, deepCopy and inspect. The primitives underneath are
 instVarNamed:, instVarNamed:put:, canUnderstand:, selectors and the
 declared instance-variable names the compiler records on each class."

"--- classes -------------------------------------------------------------------"

Object >> instVarNames
  "The instance variables a class declares itself, in declaration order."
  ^ (self __instVarNamesString substrings: ' ') asArray.

Object >> allInstVarNames
  "The instance variables of a class's instances, inherited ones first."
  | sup |
  sup := self superclass.
  ^ sup isNil
    ifTrue: [self instVarNames]
    ifFalse: [sup allInstVarNames , self instVarNames].

Object >> subclasses
  ^ (Smalltalk allClasses select: [:each | each superclass == self]) asArray.

Object >> allSubclasses
  | out |
  out := OrderedCollection new.
  self subclasses do: [:each | out add: each; addAll: each allSubclasses].
  ^ out asArray.

Object >> includesSelector: aSelector
  ^ self selectors includes: aSelector.

Object >> comment
  ^ self instVarNamed: '__comment'.

Object >> comment: aString
  self instVarNamed: '__comment' put: aString.

Object >> category
  ^ self instVarNamed: '__category'.

Object >> category: aString
  self instVarNamed: '__category' put: aString.

"--- instances -----------------------------------------------------------------"

Object >> instVarAt: index
  ^ self instVarNamed: (self class allInstVarNames at: index).

Object >> instVarAt: index put: anObject
  ^ self instVarNamed: (self class allInstVarNames at: index) put: anObject.

"deepCopy copies the receiver and, recursively, what its instance variables
 (or elements) refer to. Like Smalltalk-80's deepCopy it does not detect
 cycles: a structure that refers back to itself recurses without end."
Object >> deepCopy
  | copy |
  self isClassObject ifTrue: [^ self].
  copy := self shallowCopy.
  self class allInstVarNames do: [:name |
    copy instVarNamed: name put: (self instVarNamed: name) deepCopy].
  ^ copy.

Number >> deepCopy
  ^ self.

String >> deepCopy
  ^ self.

Character >> deepCopy
  ^ self.

Boolean >> deepCopy
  ^ self.

UndefinedObject >> deepCopy
  ^ self.

BlockClosure >> deepCopy
  ^ self.

Collection >> deepCopy
  self isClassObject ifTrue: [^ self].
  ^ self collect: [:each | each deepCopy].

Dictionary >> deepCopy
  | copy |
  self isClassObject ifTrue: [^ self].
  copy := self species new.
  self keysAndValuesDo: [:key :value | copy at: key put: value deepCopy].
  ^ copy.

"inspect prints the receiver and its instance variables on the Transcript
 and answers the receiver (the textual form of an inspector)."
Object >> inspect
  Transcript show: self printString; cr.
  self class allInstVarNames do: [:name |
    Transcript show: '  '; show: name; show: ': '; print: (self instVarNamed: name); cr].
  ^ self.

Collection >> inspect
  Transcript show: self printString; show: ' ('; print: self size; show: ' elements)'; cr.
  ^ self.
