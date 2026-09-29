"EXPECT: true false false true false an OrderedCollection(Integer Number Object) an OrderedCollection(SmallInteger Integer Number Object) an OrderedCollection(Object) an OrderedCollection(Q P Object)"
"inheritsFrom: is a strict superclass test; allSuperclasses answers the chain nearest first, withAllSuperclasses includes the receiver."
Object subclass: #P.
P subclass: #Q.
t1 := (Integer inheritsFrom: Number) printString , ' ' , (Integer inheritsFrom: Integer) printString , ' ' , (Number inheritsFrom: Integer) printString , ' ' , (Q inheritsFrom: P) printString , ' ' , (P inheritsFrom: Q) printString.
t2 := SmallInteger allSuperclasses printString , ' ' , SmallInteger withAllSuperclasses printString , ' ' , P allSuperclasses printString , ' ' , Q withAllSuperclasses printString.
t1 , ' ' , t2.
