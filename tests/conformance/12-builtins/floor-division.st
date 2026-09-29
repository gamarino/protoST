"EXPECT: -4 1 -3 -1 4 -1 3 -3"
"// floors and \\ takes the sign of the divisor (Smalltalk-80); quo: and rem:
 truncate toward zero."
(-7 // 2) printString , ' ' , (-7 \\ 2) printString , ' ' , (-7 quo: 2) printString , ' ' , (-7 rem: 2) printString , ' ' , (7 // 2 + 1) printString , ' ' , (7 \\ -2) printString , ' ' , (-17 \\ 5) printString , ' ' , (17 // -5 + 1) printString.
