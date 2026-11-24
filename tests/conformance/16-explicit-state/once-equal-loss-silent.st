"EXPECT: #(true true)"
"Spec §6.3, Q5, conformance 61: a write-once write that finds the variable
 set is silent when its value is = to the installed one, and the assignment
 answers the installed value. k1 and k2 are equal and not identical: they are
 longer than the 6 bytes protoCore keeps inline."
Object subclass: #Holder instanceVariableNames: '' writeOnceNames: 'key'.
Holder >> setKey: x
  ^ key := x

| o k1 k2 |
k1 := 'alpha-' , 'key'.
k2 := 'alpha' , '-key'.
(k1 == k2) ifTrue: [Error signal: 'k1 and k2 are identical; the test would prove nothing'].
o := Holder new.
{ (o setKey: k1) == k1. (o setKey: k2) == k1 }
