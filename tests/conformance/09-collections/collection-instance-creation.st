"EXPECT: an OrderedCollection(1 2) an OrderedCollection(1 2 3 4 5 6) an OrderedCollection(7 8) 0 3 2 #(1 2 3 4 5) #(1 2 3 4 5 6) #(7 7 7) #() 2 1"
"Class-side instance creation: the with: family through six arguments, withAll:, new: and Array new:withAll:."
t1 := (OrderedCollection with: 1 with: 2) printString , ' ' , (OrderedCollection with: 1 with: 2 with: 3 with: 4 with: 5 with: 6) printString , ' ' , (OrderedCollection withAll: #(7 8)) printString , ' ' , (OrderedCollection new: 10) size printString.
t2 := (Set with: 1 with: 2 with: 2 with: 3) size printString , ' ' , (Bag with: 1 with: 1) size printString.
t3 := (Array with: 1 with: 2 with: 3 with: 4 with: 5) printString , ' ' , (Array with: 1 with: 2 with: 3 with: 4 with: 5 with: 6) printString , ' ' , (Array new: 3 withAll: 7) printString , ' ' , (Array new: 0 withAll: 7) printString.
t4 := (Set withAll: #(4 4 5)) size printString , ' ' , ((Bag with: 9) occurrencesOf: 9) printString.
t1 , ' ' , t2 , ' ' , t3 , ' ' , t4.
