"EXPECT: an OrderedCollection(1) a Set(3) a Bag(4) #(5) 0 0 0 an OrderedCollection(1 2) #(3 4) a SortedCollection(1 2 3)"
"with: on every collection class, new: answering an empty instance, and newFrom: as a copy constructor."
t1 := (OrderedCollection with: 1) printString , ' ' , (Set with: 3) printString , ' ' , (Bag with: 4) printString , ' ' , (Array with: 5) printString.
t2 := (Set new: 5) size printString , ' ' , (Bag new: 5) size printString , ' ' , (Dictionary new: 5) size printString.
t3 := (OrderedCollection newFrom: #(1 2)) printString , ' ' , (Array newFrom: (OrderedCollection with: 3 with: 4)) printString , ' ' , (SortedCollection withAll: #(3 1 2)) printString.
t1 , ' ' , t2 , ' ' , t3.
