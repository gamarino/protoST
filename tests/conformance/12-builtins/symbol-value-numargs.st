"EXPECT: 9 7 5 0 1 2 3 1 6 -3 4 3 a Dictionary('a'->4)"
"A Symbol answers value: and value:value: by sending itself as a selector, and numArgs counts a selector's arguments. The enumerators written in the kernel (fold:, detectMax:, sum:, max:, Dictionary collect:) therefore accept a Symbol for a block."
t1 := (#squared value: 3) printString , ' ' , (#+ value: 3 value: 4) printString , ' ' , (#max: value: 3 value: 5) printString.
t2 := #squared numArgs printString , ' ' , #+ numArgs printString , ' ' , #at:put: numArgs printString , ' ' , #value:value:value: numArgs printString , ' ' , #includesSubstring: numArgs printString.
d := Dictionary new.
d at: #a put: 2.
t3 := (#(1 2 3) fold: #+) printString , ' ' , (#(1 -3 2) detectMax: #abs) printString , ' ' , (#(-1 3) sum: #abs) printString , ' ' , (#('ab' 'abc') max: #size) printString , ' ' , (d collect: #squared) printString.
t1 , ' ' , t2 , ' ' , t3.
