"EXPECT: #(7 'ada' false 'oid-1' 100 #(3) false false #(3) true)"
"Spec §11, conformance 83: deepCopy of an Account whose two constants are set,
 whose write-once variable is set and whose atomic variable holds a value
 answers a copy with equal values. The copy is built in one write before it
 is visible, so no reflective write meets a set constant or write-once
 variable. Values that are mutable objects (the owner, the notes) are copied,
 not shared, and the copy's notes evolve on their own."
Object subclass: #Person instanceVariableNames: 'name'.
Person >> name
  ^ name
Person >> name: aString
  name := aString

Object subclass: #Account instanceVariableNames: 'notes' constantNames: 'number owner'
  writeOnceNames: 'oid' atomicNames: 'balance'.
Account >> initialize
  notes := OrderedCollection new
Account >> number
  ^ number
Account >> owner
  ^ owner
Account >> oid
  ^ oid
Account >> setOid: anObject
  ^ oid := anObject
Account >> balance
  ^ balance
Account >> notes
  ^ notes

| a c |
a := Account number: 7 owner: (Person new name: 'ada'; yourself).
a setOid: 'oid-1'.
a compareAndSet: #balance from: nil to: 100.
a notes add: 3.
c := a deepCopy.
c notes add: 4.
{c number.
 c owner name.
 c owner == a owner.
 c oid.
 c balance.
 a notes asArray.
 c notes == a notes.
 c == a.
 (c notes asArray copyFrom: 1 to: 1).
 c class == Account}
