"EXPECT: 2 #(1 3 5) #(2 4) an OrderedCollection(2 4) a Set(1) 12 12 0 0 7 'abc' 'a' 5 1 9 0 (5/2) 2.5 true"
"groupedBy: answers a Dictionary of collections of the receiver's species; sum: detectSum: detectMax: detectMin: max min sum average."
g := #(1 2 3 4 5) groupedBy: [:x | x odd].
h := (OrderedCollection withAll: #(1 2 3 4)) groupedBy: [:x | x even].
k := (Set withAll: #(1 2 3)) groupedBy: [:x | x < 2].
t1 := g size printString , ' ' , (g at: true) printString , ' ' , (g at: false) printString , ' ' , (h at: true) printString , ' ' , (k at: true) printString.
t2 := (#(1 2 3) sum: [:x | x * 2]) printString , ' ' , (#(1 2 3) detectSum: [:x | x * 2]) printString , ' ' , (#() detectSum: [:x | x]) printString , ' ' , (#() sum: [:x | x]) printString.
t3 := (#(3 7 5) detectMax: [:x | x]) printString , ' ' , (#('abc' 'a' 'ab') detectMax: [:s | s size]) printString , ' ' , (#('abc' 'a' 'ab') detectMin: [:s | s size]) printString.
t4 := #(1 5 3) max printString , ' ' , #(1 5 3) min printString , ' ' , #(2 3 4) sum printString , ' ' , #() sum printString , ' ' , #(1 2 3 4) average printString , ' ' , #(2.0 3.0) average printString.
emptyAvg := [#() average. false] on: Error do: [:e | true].
t1 , ' ' , t2 , ' ' , t3 , ' ' , t4 , ' ' , emptyAvg printString.
