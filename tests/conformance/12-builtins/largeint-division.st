"EXPECT: 9900 1024 0 true true 1024 0 true"
"Division, floor division, remainder, asFloat and sqrt across the integer tower."
a := 100 factorial / 98 factorial.
b := (2 raisedTo: 100) // (2 raisedTo: 90).
c := (2 raisedTo: 100) \\ (2 raisedTo: 90).
d := ((2 raisedTo: 100) asFloat - 1.2676506002282294e30) abs < 1e15.
e := ((2 raisedTo: 100) sqrt - 1125899906842624) abs < 1.
f := (2 raisedTo: 100) quo: (2 raisedTo: 90).
g := (2 raisedTo: 100) rem: (2 raisedTo: 90).
h := ((2 raisedTo: 100) gcd: (2 raisedTo: 90)) = (2 raisedTo: 90).
a printString , ' ' , b printString , ' ' , c printString , ' ' , (d ifTrue: ['true'] ifFalse: ['false']) , ' ' , (e ifTrue: ['true'] ifFalse: ['false']) , ' ' , f printString , ' ' , g printString , ' ' , (h ifTrue: ['true'] ifFalse: ['false']).
