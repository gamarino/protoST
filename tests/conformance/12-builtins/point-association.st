"EXPECT: 3@4 4@6 5.0 true #aaaaaaaa->1 1 6@8"
"Points (x @ y) and Associations (key -> value)."
p := 3 @ 4.
q := p + (1 @ 2).
d := (0 @ 0) dist: p.
a := #aaaaaaaa -> 1.
p printString , ' ' , q printString , ' ' , d printString , ' ' , (p = (3 @ 4)) printString , ' ' , a printString , ' ' , a value printString , ' ' , (p * 2) printString.
