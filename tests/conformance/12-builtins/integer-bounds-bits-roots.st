"EXPECT: 36028797018963967 SmallInteger LargeInteger -36028797018963968 SmallInteger LargeInteger 0 1 3 8 9 101 true false true 0 1 3 4 4 100000000000000000000 99999999999999999999 true false"
"SmallInteger maxVal/minVal bound the 56-bit tagged representation; highBit, isBitSet:, sqrtFloor (exact for LargeIntegers) and isProbablyPrime."
t1 := SmallInteger maxVal printString , ' ' , SmallInteger maxVal class name , ' ' , (SmallInteger maxVal + 1) class name , ' ' , SmallInteger minVal printString , ' ' , SmallInteger minVal class name , ' ' , (SmallInteger minVal - 1) class name.
t2 := 0 highBit printString , ' ' , 1 highBit printString , ' ' , 5 highBit printString , ' ' , 255 highBit printString , ' ' , 256 highBit printString , ' ' , (2 raisedTo: 100) highBit printString.
t3 := (5 isBitSet: 1) printString , ' ' , (5 isBitSet: 2) printString , ' ' , (5 isBitSet: 3) printString.
t4 := 0 sqrtFloor printString , ' ' , 1 sqrtFloor printString , ' ' , 15 sqrtFloor printString , ' ' , 16 sqrtFloor printString , ' ' , 17 sqrtFloor printString , ' ' , (10 raisedTo: 40) sqrtFloor printString , ' ' , ((10 raisedTo: 40) - 1) sqrtFloor printString.
t5 := 97 isProbablyPrime printString , ' ' , 91 isProbablyPrime printString.
t1 , ' ' , t2 , ' ' , t3 , ' ' , t4 , ' ' , t5.
