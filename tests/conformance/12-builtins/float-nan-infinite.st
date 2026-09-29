"EXPECT: true false false false false true true false false false false"
"isNaN and isInfinite answer for every Number: only a Float can be NaN or infinite."
t1 := Float nan isNaN printString , ' ' , 1.5 isNaN printString , ' ' , 3 isNaN printString , ' ' , (1/2) isNaN printString , ' ' , Float infinity isNaN printString.
t2 := Float infinity isInfinite printString , ' ' , Float infinity negated isInfinite printString , ' ' , Float nan isInfinite printString , ' ' , 1.0e300 isInfinite printString , ' ' , 5 isInfinite printString , ' ' , (2 raisedTo: 100) isInfinite printString.
t1 , ' ' , t2.
