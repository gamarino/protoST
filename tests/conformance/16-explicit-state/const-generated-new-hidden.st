"EXPECT: #(true true 'Account class' 'Metaclass' #(#number:) false 'Account class' 'Account class' #() true 'Object class' true 'Account class' true 'Sorted2 class' 'Account class')"
"Maintainer ruling of 2026-11-23 (round 3): the generated new of a class with
 constants lives in a hidden parent of its metaclass, never on the metaclass
 itself, and reflection never shows it. The metaclass's superclass is the
 superclass's metaclass, its class is Metaclass, its printString names the
 class, its selectors are the class-side methods it holds (the generated
 constructor, not new), it does not include new, and lookupSelector: #new
 answers the metaclass (or, for a subclass that inherits it, the metaclass
 that has it), never an unnamed object. The same holds after a user new is
 added, for a kernel new above it (Sorted2), and for the class answered by
 printString inside a collection."
Object subclass: #Account instanceVariableNames: 'h' constantNames: 'number'.
Account subclass: #Sub instanceVariableNames: 's'.
SortedCollection subclass: #Sorted2 instanceVariableNames: '' constantNames: 'k'.
Object subclass: #Owned instanceVariableNames: '' constantNames: 'o'.
Owned class >> new
  ^ super new

{Account class superclass == Object class.
 Sub class superclass == Account class.
 Account class printString.
 Account class class printString.
 Account class selectors asArray.
 (Account class includesSelector: #new).
 (Account class lookupSelector: #new) printString.
 (Sub class lookupSelector: #new) printString.
 Account class allInstVarNames.
 (Account class inheritsFrom: Object class).
 Account class allSuperclasses first printString.
 (Owned class selectors asSet = #(#o: #new) asSet).
 (Sub class allSuperclasses first) printString.
 (Owned class includesSelector: #new).
 (Sorted2 class lookupSelector: #new) printString.
 (Array with: Account class) first printString}
