"EXPECT: 4 Dictionary #(1 2 3) 2 0 2 nil #(9 9 2 9 9 3) #(0 3 0) an OrderedCollection(0 3) #(1 2) #(1 2 3) an OrderedCollection(1 2 3) 3 1 4 3 3"
"asDictionary, keysSortedSafely (mixed keys do not fail), at:ifAbsent: and copyReplaceAll:with: on sequences, Array/OrderedCollection streamContents:, and the block forms of max: and min: (answering the extreme value, as Pharo does)."
d := (Array with: 1 -> 2 with: 3 -> 4) asDictionary.
k := Dictionary new.
k at: 3 put: 0; at: 1 put: 0; at: 2 put: 0.
m := Dictionary new.
m at: 'b' put: 0; at: 2 put: 0.
t1 := (d at: 3) printString , ' ' , d class name , ' ' , k keysSortedSafely printString , ' ' , m keysSortedSafely size printString.
t2 := (#(1 2) at: 5 ifAbsent: [0]) printString , ' ' , (#(1 2) at: 2 ifAbsent: [0]) printString , ' ' , ((1 to: 3) at: 4 ifAbsent: [nil]) printString.
t3 := (#(1 2 1 3) copyReplaceAll: #(1) with: #(9 9)) printString , ' ' , (#(1 2 3 1 2) copyReplaceAll: #(1 2) with: #(0)) printString , ' ' , ((OrderedCollection withAll: #(1 2 3)) copyReplaceAll: #(1 2) with: #(0)) printString , ' ' , (#(1 2) copyReplaceAll: #() with: #(7)) printString.
t4 := (Array streamContents: [:s | s nextPut: 1; nextPutAll: #(2 3)]) printString , ' ' , (OrderedCollection streamContents: [:s | s nextPut: 1; nextPut: 2; nextPut: 3]) printString.
t5 := (#('a' 'bbb' 'cc') max: [:e | e size]) printString , ' ' , (#('a' 'bbb' 'cc') min: [:e | e size]) printString , ' ' , (3 max: 4) printString , ' ' , (3 min: 4) printString , ' ' , #(3 1 2) max printString.
t1 , ' ' , t2 , ' ' , t3 , ' ' , t4 , ' ' , t5.
