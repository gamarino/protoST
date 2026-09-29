"EXPECT: 1 1 2"
"Each activation of a block has its own temporaries."
mk := [| n | n := 0. [n := n + 1]].
c1 := mk value.
c2 := mk value.
a := c1 value.
b := c2 value.
c := c1 value.
a printString , ' ' , b printString , ' ' , c printString.
