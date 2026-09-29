"EXPECT: #(1 4 9) #(2 4) #(1 3) 6 2 true true 2 14 an OrderedCollection(3 1) #(4 9)"
"A Symbol stands in for a block in the enumeration protocol, as in Pharo: a non-block argument is sent value: / value:value:."
oc := OrderedCollection withAll: #(-3 1).
t1 := (#(1 2 3) collect: #squared) printString , ' ' , (#(1 2 3 4) select: #even) printString , ' ' , (#(1 2 3) reject: #even) printString.
t2 := (#(1 2 3) inject: 0 into: #+) printString , ' ' , (#(1 2 3) detect: #even) printString , ' ' , (#(1 2 3) anySatisfy: #even) printString , ' ' , (#(1 2 3) allSatisfy: #isInteger) printString.
t3 := (#(1 2 3) count: #odd) printString , ' ' , ((Set withAll: #(1 2 3)) collect: #squared) sum printString , ' ' , (oc collect: #abs) printString , ' ' , ((2 to: 3) collect: #squared) printString.
t1 , ' ' , t2 , ' ' , t3.
