"EXPECT: 10 10 5 6 #(1 10 2 20) an OrderedCollection(1 1 2 2) Set 3 true Set #(2 3) #(2 3) #(1 3) Set #(1 3)"
"fold:/reduce: combine the elements pairwise; flatCollect: concatenates the block results into the receiver's species; union: answers a Set, intersection: and difference: keep the receiver's species (a Set for a Set)."
s := Set withAll: #(1 2 3).
t1 := (#(1 2 3 4) fold: [:a :b | a + b]) printString , ' ' , (#(1 2 3 4) reduce: [:a :b | a + b]) printString , ' ' , (#(5) fold: [:a :b | a + b]) printString , ' ' , (s fold: [:a :b | a + b]) printString.
t2 := (#(1 2) flatCollect: [:x | Array with: x with: x * 10]) printString , ' ' , ((OrderedCollection withAll: #(1 2)) flatCollect: [:x | Array with: x with: x]) printString.
u := #(1 2) union: #(2 3).
t3 := u class name , ' ' , u size printString , ' ' , ((s union: #(4)) includes: 4) printString.
i := s intersection: #(2 3 4).
t4 := i class name , ' ' , i asArray sorted printString , ' ' , (#(1 2 3 2) intersection: #(2 3)) sorted printString , ' ' , (#(1 2 3 2) difference: #(2)) printString.
d := s difference: #(2).
t5 := d class name , ' ' , d asArray sorted printString.
t1 , ' ' , t2 , ' ' , t3 , ' ' , t4 , ' ' , t5.
