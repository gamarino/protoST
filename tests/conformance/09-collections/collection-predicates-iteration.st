"EXPECT: 2 true false true false true false #(5 7 9) 4-10-18 321 a1b2 1a2b 3 0 true"
"count: allSatisfy: anySatisfy: noneSatisfy:, with:collect: with:do:, reverseDo:, doWithIndex:/withIndexDo:, keysAndValuesDo: on sequences."
a := #(1 2 3).
t1 := (a count: [:x | x odd]) printString , ' ' , (a allSatisfy: [:x | x > 0]) printString , ' ' , (a allSatisfy: [:x | x > 1]) printString.
t2 := (a anySatisfy: [:x | x > 2]) printString , ' ' , (a anySatisfy: [:x | x > 3]) printString , ' ' , (a noneSatisfy: [:x | x > 3]) printString , ' ' , (a noneSatisfy: [:x | x > 2]) printString.
w := WriteStream on: String new.
a with: #(4 5 6) do: [:x :y | w isEmpty ifFalse: [w nextPut: $-]. w print: x * y].
r := WriteStream on: String new.
a reverseDo: [:x | r print: x].
i := WriteStream on: String new.
#($a $b) doWithIndex: [:c :n | i nextPut: c; print: n].
j := WriteStream on: String new.
#($a $b) keysAndValuesDo: [:n :c | j print: n; nextPut: c].
m := 0.
(OrderedCollection withAll: #(5 6 7)) withIndexDo: [:x :n | m := m + n].
e := 0.
#() noneSatisfy: [:x | e := e + 1].
t3 := (a with: #(4 5 6) collect: [:x :y | x + y]) printString , ' ' , w contents , ' ' , r contents , ' ' , i contents , ' ' , j contents.
t4 := (m = 6 ifTrue: ['3'] ifFalse: ['?']) , ' ' , e printString , ' ' , (#() noneSatisfy: [:x | true]) printString.
t1 , ' ' , t2 , ' ' , t3 , ' ' , t4.
