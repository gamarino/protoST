"EXPECT: #(1 1 6)"
"§6.4: the receiver of an inlined to:do: is evaluated once, and the value of
 the expression is the receiver."
| c r sum |
c := 0.
sum := 0.
r := (c := c + 1) to: 3 do: [ :i | sum := sum + i ].
{ c. r. sum }
