"EXPECT: (1 to: 3) (1 to: 0) (5 to: 1) (1 to: 10 by: 2) (10 to: 1 by: -3) (1 to: 2 by: 0.5) ((1/2) to: 2) (-3 to: 3) (1 to: 1) (-1 to: -5 by: -2)"
"Intervals print as the expression that built them (as Pharo does), empty and single-element ones included."
t1 := (1 to: 3) printString , ' ' , (1 to: 0) printString , ' ' , (5 to: 1) printString , ' ' , (1 to: 10 by: 2) printString , ' ' , (10 to: 1 by: -3) printString.
t2 := (1 to: 2 by: 0.5) printString , ' ' , ((1/2) to: 2) printString , ' ' , (-3 to: 3) printString , ' ' , (1 to: 1) printString , ' ' , (-1 to: -5 by: -2) printString.
t1 , ' ' , t2.
