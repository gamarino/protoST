"EXPECT: 7 true false none"
"Smalltalk at: / at:put: / includesKey: / at:ifAbsent: reach the globals."
Smalltalk at: #Answer put: 7.
(Smalltalk at: #Answer) printString , ' ' , (Smalltalk includesKey: #Answer) printString , ' ' , (Smalltalk includesKey: #NoSuchGlobalHere) printString , ' ' , (Smalltalk at: #NoSuchGlobalHere ifAbsent: ['none']).
