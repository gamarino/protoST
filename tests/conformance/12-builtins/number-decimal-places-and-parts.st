"EXPECT: 3.14 2.000 0.3333 0.67 -1.5 0.00 4 1180591620717411303424.0 0.75 3.0 -0.75 -3.0 0 7 (1/2) 3 (-1/2) -3 3 -3"
"printShowingDecimalPlaces: on Float, Integer, LargeInteger and Fraction; fractionPart and integerPart; Float asInteger truncates."
t1 := (3.14159 printShowingDecimalPlaces: 2) , ' ' , (2 printShowingDecimalPlaces: 3) , ' ' , ((1/3) printShowingDecimalPlaces: 4) , ' ' , ((2/3) printShowingDecimalPlaces: 2) , ' ' , (-1.5 printShowingDecimalPlaces: 1) , ' ' , (-0.001 printShowingDecimalPlaces: 2) , ' ' , (3.7 printShowingDecimalPlaces: 0) , ' ' , ((2 raisedTo: 70) printShowingDecimalPlaces: 1).
t2 := 3.75 fractionPart printString , ' ' , 3.75 integerPart printString , ' ' , -3.75 fractionPart printString , ' ' , -3.75 integerPart printString , ' ' , 7 fractionPart printString , ' ' , 7 integerPart printString.
t3 := (7/2) fractionPart printString , ' ' , (7/2) integerPart printString , ' ' , (-7/2) fractionPart printString , ' ' , (-7/2) integerPart printString , ' ' , 3.7 asInteger printString , ' ' , -3.7 asInteger printString.
t1 , ' ' , t2 , ' ' , t3.
