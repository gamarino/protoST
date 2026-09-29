"EXPECT: true 2 10 30 true 1 30 true 1 10 3 0"
"Dictionary collect:, select: and reject: answer a Dictionary with the same keys (Pharo); the receiver is unchanged."
d := Dictionary new.
d at: 1 put: 1; at: 3 put: 3.
c := d collect: [:v | v * 10].
s := (Dictionary newFrom: {1->10. 3->30}) select: [:v | v > 10].
r := (Dictionary newFrom: {1->10. 3->30}) reject: [:v | v > 10].
e := d select: [:v | false].
t1 := (c isKindOf: Dictionary) printString , ' ' , c size printString , ' ' , (c at: 1) printString , ' ' , (c at: 3) printString.
t2 := (s isKindOf: Dictionary) printString , ' ' , s size printString , ' ' , (s at: 3) printString.
t3 := (r isKindOf: Dictionary) printString , ' ' , r size printString , ' ' , (r at: 1) printString , ' ' , (d at: 3) printString , ' ' , e size printString.
t1 , ' ' , t2 , ' ' , t3.
