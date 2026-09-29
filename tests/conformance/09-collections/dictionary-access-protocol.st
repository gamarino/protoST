"EXPECT: 20 nil 20 absent 7 7 1 3 7 none 2 1->10 3->30 true true false 10"
"at:ifPresent: at:ifPresent:ifAbsent: at:ifAbsentPut: keyAtValue: removeKey:ifAbsent: keysAndValuesDo: associationsDo: includes: anyOne."
d := Dictionary new.
d at: 1 put: 10; at: 3 put: 30.
t1 := (d at: 1 ifPresent: [:v | v * 2]) printString , ' ' , (d at: 9 ifPresent: [:v | v * 2]) printString , ' ' , (d at: 1 ifPresent: [:v | v * 2] ifAbsent: ['absent']) printString , ' ' , (d at: 9 ifPresent: [:v | v * 2] ifAbsent: ['absent']).
n := 0.
x := d at: 5 ifAbsentPut: [n := n + 1. 7].
y := d at: 5 ifAbsentPut: [n := n + 1. 99].
t2 := x printString , ' ' , y printString , ' ' , n printString , ' ' , (d keyAtValue: 30) printString , ' ' , (d removeKey: 5 ifAbsent: [0]) printString , ' ' , (d removeKey: 5 ifAbsent: ['none']) , ' ' , d size printString.
sum := 0.
d keysAndValuesDo: [:k :v | sum := sum + k + v].
assocs := SortedCollection sortBlock: [:p :q | p key <= q key].
d associationsDo: [:a | assocs add: a].
t3 := assocs first printString , ' ' , assocs last printString , ' ' , (sum = 44) printString , ' ' , (d includes: 30) printString , ' ' , (d includes: 3) printString , ' ' , (Dictionary new at: 1 put: 10; yourself) anyOne printString.
t1 , ' ' , t2 , ' ' , t3.
