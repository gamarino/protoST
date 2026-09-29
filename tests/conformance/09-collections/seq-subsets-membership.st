"EXPECT: #(1 2) #(3 4) #(2 3 4) #(1 2 3) #(3 4) #(1 2) true false true false #(1 2 3 4 5) #(1 3 4) an OrderedCollection(1 3) 3 -1 2"
"first: last: allButFirst allButLast (also with a count), includesAll: includesAny:, copyWith: copyWithout:, indexOf:ifAbsent:."
a := #(1 2 3 4).
t1 := (a first: 2) printString , ' ' , (a last: 2) printString , ' ' , a allButFirst printString , ' ' , a allButLast printString , ' ' , (a allButFirst: 2) printString , ' ' , (a allButLast: 2) printString.
t2 := (a includesAll: #(4 1)) printString , ' ' , (a includesAll: #(1 9)) printString , ' ' , (a includesAny: #(9 3)) printString , ' ' , (a includesAny: #()) printString.
t3 := (a copyWith: 5) printString , ' ' , (#(1 2 3 2 4) copyWithout: 2) printString , ' ' , ((OrderedCollection withAll: #(1 2 3 2)) copyWithout: 2) printString.
t4 := (a indexOf: 3 ifAbsent: [0]) printString , ' ' , (a indexOf: 9 ifAbsent: [-1]) printString , ' ' , ((Set with: 1) copyWith: 2) size printString.
t1 , ' ' , t2 , ' ' , t3 , ' ' , t4.
