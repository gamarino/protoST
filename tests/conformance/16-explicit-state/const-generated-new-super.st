"EXPECT: #(#(1 3) 'k' true true 'the constant ''k'' of a Forgets is not set at the end of construction (new)' true)"
"Maintainer ruling of 2026-11-23: the generated new of a class with
 constants runs super new, bound to the metaclass it is installed on, then
 the end-of-construction check. So a SortedCollection subclass keeps the
 kernel new (a default sort block) and is checked after it; a class under a
 superclass without constants that has a user new of its own runs that new
 first (Base class >> new, allowed: Base has no constants), then the check.
 Object class >> new is the 0.16.1 method and checks nothing (Plain)."
SortedCollection subclass: #Sorted2 instanceVariableNames: '' constantNames: 'k'.
Sorted2 >> initialize
  k := 'k'
Sorted2 >> k
  ^ k
Object subclass: #Base instanceVariableNames: 'b'.
Base class >> new
  ^ super new setB
Base >> setB
  b := true
Base >> b
  ^ b
Base subclass: #Sets instanceVariableNames: '' constantNames: 'k'.
Sets >> initialize
  k := 1
Base subclass: #Forgets instanceVariableNames: '' constantNames: 'k'.
Object subclass: #Plain instanceVariableNames: 'p'.

{(Sorted2 new add: 3; add: 1; yourself) asArray.
 Sorted2 new k.
 (Sets new instVarIsSet: #k).
 Sets new b.
 [Forgets new] on: Error do: [:e | e messageText].
 Plain class selectors isEmpty}
