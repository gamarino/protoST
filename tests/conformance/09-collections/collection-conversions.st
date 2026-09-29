"EXPECT: true false false false 1 2 true 3 2 a SortedCollection(1 2 3) a SortedCollection(3 2 1) #(1 2) an OrderedCollection(3 4)"
"isArray, readStream over sequences, asBag, asSet, asSortedCollection, asSortedCollection:."
rs := #(1 2 3) readStream.
x := rs next.
y := rs next.
oc := OrderedCollection withAll: #(3 4).
t1 := #(1) isArray printString , ' ' , (OrderedCollection new) isArray printString , ' ' , 'ab' isArray printString , ' ' , 3 isArray printString.
t2 := x printString , ' ' , y printString , ' ' , (rs next = 3 and: [rs atEnd]) printString , ' ' , #(1 1 2) asBag size printString , ' ' , #(1 1 2) asSet size printString.
t3 := #(3 1 2) asSortedCollection printString , ' ' , (#(1 3 2) asSortedCollection: [:p :q | p >= q]) printString , ' ' , (1 to: 2) readStream upToEnd printString , ' ' , oc readStream upToEnd printString.
t1 , ' ' , t2 , ' ' , t3.
