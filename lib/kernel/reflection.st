"Kernel: reflection. Instance-variable access by name or index, the
 instance-variable and subclass lists of a class, class comments and
 categories, deepCopy and inspect. The primitives underneath are
 instVarNamed:, instVarNamed:put:, canUnderstand:, selectors, the declared
 instance-variable names the compiler records on each class, and the private
 __rawInstVarNamed: and __copyWithInstVarValues: of deepCopy and inspect."

"--- classes -------------------------------------------------------------------"

Behavior >> instVarNames
  "The instance variables a class declares itself, in declaration order."
  ^ (self __instVarNamesString substrings: ' ') asArray.

"Pharo's metaclass predicates (D52). A metaclass's soleInstance (and
 instanceClass) is primitive."
Behavior >> isMeta
  ^ false.

Metaclass >> isMeta
  ^ true.

Behavior >> allInstVarNames
  "The instance variables of a class's instances, inherited ones first, in the
   runtime's order: the superclass's, then each mixin's (uses:, addBehavior:)
   in listed order, then the class's own; each name once. It does not follow
   superclass, which answers the first mixin once a class uses one. A
   metaclass lists its class-instance variables, its superclass's first."
  | sup |
  self isMeta ifFalse: [^ (self __allInstVarNamesString substrings: ' ') asArray].
  sup := self superclass.
  ^ sup isNil
    ifTrue: [self instVarNames]
    ifFalse: [sup allInstVarNames , self instVarNames].

Behavior >> subclasses
  ^ (Smalltalk allClasses select: [:each | each superclass == self]) asArray.

Behavior >> allSubclasses
  | out |
  out := OrderedCollection new.
  self subclasses do: [:each | out add: each; addAll: each allSubclasses].
  ^ out asArray.

Behavior >> allSuperclasses
  "The superclasses of a class, nearest first, in an OrderedCollection."
  | out c |
  out := OrderedCollection new.
  c := self superclass.
  [c notNil] whileTrue: [out add: c. c := c superclass].
  ^ out.

Behavior >> withAllSuperclasses
  ^ self allSuperclasses addFirst: self; yourself.

Behavior >> inheritsFrom: aClass
  "Whether aClass is an ancestor of the receiver, a class (not the class
   itself): a superclass, or a mixin added by `uses:` or addBehavior:, which
   is a parent class in protoST (LANGUAGE.md §4.11). allSuperclasses lists
   the primary superclass chain only."
  ^ self __hasAncestor: aClass.

"allInstances is not provided: neither protoCore nor the runtime exposes an
 enumeration of the live objects of a class."

Behavior >> includesSelector: aSelector
  ^ self selectors includes: aSelector.

Behavior >> comment
  ^ self instVarNamed: '__comment'.

Behavior >> comment: aString
  self instVarNamed: '__comment' put: aString.

Behavior >> category
  ^ self instVarNamed: '__category'.

Behavior >> category: aString
  self instVarNamed: '__category' put: aString.

"A class is not copied: deepCopy of a class answers the class."
Behavior >> deepCopy
  ^ self.

"--- instances -----------------------------------------------------------------"

Object >> instVarAt: index
  ^ self instVarNamed: (self class allInstVarNames at: index).

Object >> instVarAt: index put: anObject
  ^ self instVarNamed: (self class allInstVarNames at: index) put: anObject.

"deepCopy copies the receiver and, recursively, what its instance variables
 (or elements) refer to. Like Smalltalk-80's deepCopy it does not detect
 cycles: a structure that refers back to itself recurses without end.

 The copy is built with all its variables in one write, before anyone can
 see it (explicit-state spec §11, plan decision D-h), so a constant or a
 write-once variable that is set is copied, not refused by a reflective
 write, and an atomic variable gets its value read once. The values are read
 without the constant check, so an unset constant stays unset in the copy
 and signals when the copy reads it."
Object >> deepCopy
  ^ self __copyWithInstVarValues:
      (self class allInstVarNames collect: [:name | (self __rawInstVarNamed: name) deepCopy]).

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
  ^ self collect: [:each | each deepCopy].

Dictionary >> deepCopy
  | copy |
  copy := self species new.
  self keysAndValuesDo: [:key :value | copy at: key put: value deepCopy].
  ^ copy.

"inspect prints the receiver and its instance variables on the Transcript
 and answers the receiver (the textual form of an inspector). Inspecting is
 not a program read: an unset constant prints as <unset constant> and
 signals nothing (plan decisions D-h and G4, as the debugger's row of
 explicit-state spec §6.2)."
Object >> inspect
  Transcript show: self printString; cr.
  self class allInstVarNames do: [:name | | value |
    value := self __rawInstVarNamed: name.
    Transcript show: '  '; show: name; show: ': '.
    (value isNil and: [self class isMeta not
        and: [(self class instVarModifierOf: name) = #constant]])
      ifTrue: [Transcript show: '<unset constant>']
      ifFalse: [Transcript print: value].
    Transcript cr].
  ^ self.

Collection >> inspect
  Transcript show: self printString; show: ' ('; print: self size; show: ' elements)'; cr.
  ^ self.
