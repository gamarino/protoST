"EXPECT: (1/2) (3/4) 1 0.75 true (1/3) (7/6) true 2 (-1/2) 3/4"
"Integer division answers an exact Fraction when it is not exact (0.4.0)."
a := (1/3) + (1/6).
b := 3/4.
c := (3/4) * 4/3.
d := (3/4) asFloat.
e := (1/2) < (2/3).
f := (1/3) printString.
g := (1/2) + (2/3).
h := ((1/3) + 0.5 - 0.8333333333333334) abs < 1e-12.
i := 6/3.
j := 1 / -2.
k := (3/4) displayString.
a printString , ' ' , b printString , ' ' , c printString , ' ' , d printString , ' ' , e printString , ' ' , f , ' ' , g printString , ' ' , h printString , ' ' , i printString , ' ' , j printString , ' ' , k.
