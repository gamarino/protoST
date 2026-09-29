"EXPECT: an OrderedCollection(9 1 2 8 3 7) 9 #(1 2) an OrderedCollection(3) #(4 5) an OrderedCollection(3) an OrderedCollection(8 9 1 2 3) an OrderedCollection(1 2 3 8 9) #(8 9) true true 2 an OrderedCollection(1 3)"
"add:beforeIndex:, removeFirst:/removeLast: (answer an Array, in order), addAllFirst:/addAllLast: (answer the argument), removeAt:."
a := OrderedCollection withAll: #(1 2 3).
r := a add: 9 beforeIndex: 1.
a add: 8 beforeIndex: 4.
a add: 7 beforeIndex: 6.
b := OrderedCollection withAll: #(1 2 3).
c := OrderedCollection withAll: #(3 4 5).
d := OrderedCollection withAll: #(1 2 3).
d addAllFirst: #(8 9).
e := OrderedCollection withAll: #(1 2 3).
f := e addAllLast: #(8 9).
bad1 := [(OrderedCollection with: 1) add: 0 beforeIndex: 3. false] on: Error do: [:x | true].
bad2 := [(OrderedCollection with: 1) removeFirst: 2. false] on: Error do: [:x | true].
g := OrderedCollection withAll: #(1 2 3).
t1 := a printString , ' ' , r printString , ' ' , (b removeFirst: 2) printString , ' ' , b printString , ' ' , (c removeLast: 2) printString , ' ' , c printString.
t2 := d printString , ' ' , e printString , ' ' , f printString , ' ' , bad1 printString , ' ' , bad2 printString , ' ' , (g removeAt: 2) printString , ' ' , g printString.
t1 , ' ' , t2.
