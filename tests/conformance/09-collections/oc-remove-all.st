"EXPECT: an OrderedCollection(1 2 2) #(2 3) an OrderedCollection(1 3 5) an OrderedCollection() a Set(1) true"
"removeAll: removes each element (answering the argument), removeAllSuchThat: removes every match, removeAll empties."
oc := OrderedCollection withAll: #(1 2 3 2 2).
r := oc removeAll: #(2 3).
oc2 := OrderedCollection withAll: #(1 2 3 4 5 6).
oc2 removeAllSuchThat: [:x | x even].
oc3 := OrderedCollection withAll: #(1 2).
oc3 removeAll.
s := Set withAll: #(1 2 3).
s removeAllSuchThat: [:x | x > 1].
missing := [(OrderedCollection with: 1) removeAll: #(9). false] on: Error do: [:e | true].
oc printString , ' ' , r printString , ' ' , oc2 printString , ' ' , oc3 printString , ' ' , s printString , ' ' , missing printString.
