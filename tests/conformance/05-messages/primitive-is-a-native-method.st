"EXPECT: #('3' 'a primitive method takes no named arguments' 7 true true 'Number' false)"
"A primitive is a protoCore native method bound under its selector (0.10.0): a call-form send reaches it with positional values and is refused with named ones; perform:, respondsTo:, lookupSelector: and an override work as for any method."
Object subclass: #Shadow.
Shadow >> printString ^ 'shadowed'.
{ 3 printString().
  [ 3 printString(base = 16) ] on: Error do: [ :e | e messageText ].
  3 perform: #+ with: 4.
  3 respondsTo: #+.
  #(1 2) respondsTo: #at:put:.
  (SmallInteger lookupSelector: #+) printString.
  Shadow new printString = Object new printString }
