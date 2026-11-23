"EXPECT: #(true true 'eq' true 'not a Boolean' true true)"
"Spec §6.3, conformance 63: the = that judges a losing write is the installed
 value's, sent to it with the written value. An = that answers true for
 everything makes any second write silent, and the write answers the first
 value. An = that signals propagates its error, and the variable keeps the
 first value. An = that answers a non-Boolean signals NonBooleanReceiver. An
 identical value is silent without sending =."
Object subclass: #Holder instanceVariableNames: '' writeOnceNames: 'key'.
Holder >> setKey: x
  ^ key := x
Holder >> key
  ^ key
Object subclass: #AllEqual instanceVariableNames: ''.
AllEqual >> = other
  ^ true
Object subclass: #BadEq instanceVariableNames: ''.
BadEq >> = other
  ^ Error signal: 'eq'
Object subclass: #ThreeEq instanceVariableNames: ''.
ThreeEq >> = other
  ^ 3

| o1 first o2 bad badResult o3 three threeResult |
o1 := Holder new.
first := AllEqual new.
o1 setKey: first.
o2 := Holder new.
bad := BadEq new.
o2 setKey: bad.
badResult := [o2 setKey: BadEq new. 'no error'] on: Error do: [:e | e messageText].
o3 := Holder new.
three := ThreeEq new.
o3 setKey: three.
threeResult := [o3 setKey: ThreeEq new. 'no error'] on: NonBooleanReceiver do: [:e | 'not a Boolean'].
{ (o1 setKey: AllEqual new) == first. o1 key == first.
  badResult. o2 key == bad.
  threeResult. o3 key == three.
  (o3 setKey: three) == three }
