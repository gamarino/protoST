"EXPECT: true false true true false true false 3 0 7 0 3 0 -1 2 0"
"includesSubString: (Pharo spelling) and includesSubstring:, beginsWith: endsWith:, findString: (and startingAt:), indexOf: (and ifAbsent:), occurrencesOf:."
s := 'hello hello'.
t1 := (s includesSubString: 'llo h') printString , ' ' , (s includesSubString: 'xyz') printString , ' ' , (s includesSubstring: 'ell') printString , ' ' , (s beginsWith: 'hel') printString , ' ' , (s beginsWith: 'ell') printString , ' ' , (s endsWith: 'llo') printString , ' ' , ('lo' endsWith: 'hello') printString.
t2 := (s findString: 'llo') printString , ' ' , (s findString: 'xyz') printString , ' ' , (s findString: 'hel' startingAt: 2) printString , ' ' , (s findString: '') printString.
t3 := (s indexOf: $l) printString , ' ' , (s indexOf: $z) printString , ' ' , (s indexOf: $z ifAbsent: [-1]) printString , ' ' , ('hello' occurrencesOf: $l) printString , ' ' , ('hello' occurrencesOf: $z) printString.
t1 , ' ' , t2 , ' ' , t3.
