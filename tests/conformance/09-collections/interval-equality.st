"EXPECT: true true true true false false true false true"
"Sequences compare by species and elements in order: an Interval equals the Array of its elements (Pharo); an OrderedCollection does not."
t1 := ((1 to: 3) = #(1 2 3)) printString , ' ' , (#(1 2 3) = (1 to: 3)) printString , ' ' , ((1 to: 3) = (1 to: 3)) printString , ' ' , ((1 to: 0) = #()) printString.
t2 := ((1 to: 3) = #(1 2)) printString , ' ' , ((1 to: 3) = (OrderedCollection withAll: #(1 2 3))) printString , ' ' , ((1 to: 5 by: 2) = #(1 3 5)) printString.
t3 := (#(1 2) = (OrderedCollection withAll: #(1 2))) printString , ' ' , ((1 to: 3) hash = #(1 2 3) hash) printString.
t1 , ' ' , t2 , ' ' , t3.
