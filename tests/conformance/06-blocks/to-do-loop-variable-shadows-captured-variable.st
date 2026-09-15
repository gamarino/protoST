"EXPECT: 6 5"
"§6.3: a to:do: block argument named like a captured module variable is a"
"distinct variable. The loop sums 1..3; the earlier block still reads 5."
"Before the D29 fix the inlined loop body read the captured variable (15 5)."
i := 5.
b := [ i ].
s := 0.
1 to: 3 do: [ :i | s := s + i ].
s printString, ' ', b value printString.
