"EXPECT: #(1 1 1 1 #(1 2 3) true #() true)"
"Task 6 ruling: the class-side new of SortedCollection, Random, WriteStream
 and UUID sends initialize, as Object class>>new does, before the
 end-of-construction check. A user subclass whose initialize sets its
 constant can then be made with new; before, new skipped initialize and
 signalled M13 for the constant. The kernel classes still work as before."
| m |
m := Import from: 'uuid'.
SortedCollection subclass: #S2 instanceVariableNames: '' constantNames: 'k'.
S2 >> initialize
  k := 1
S2 >> k
  ^ k

Random subclass: #R2 instanceVariableNames: '' constantNames: 'k'.
R2 >> initialize
  k := 1
R2 >> k
  ^ k

WriteStream subclass: #W2 instanceVariableNames: '' constantNames: 'k'.
W2 >> initialize
  k := 1
W2 >> k
  ^ k

UUID yourself subclass: #U2
  instanceVariableNames: ''
  constantNames: 'k'
  writeOnceNames: ''
  atomicNames: ''.
U2 >> initialize
  k := 1
U2 >> k
  ^ k

| s |
s := [S2 new] on: Error do: [:e | e messageText].
{s k.
 ([R2 new] on: Error do: [:e | e messageText]) k.
 ([W2 new] on: Error do: [:e | e messageText]) k.
 ([U2 new] on: Error do: [:e | e messageText]) k.
 (s add: 3; add: 1; add: 2; yourself) asArray.
 (Random new next between: 0 and: 1).
 WriteStream new contents.
 UUID new class == UUID}
