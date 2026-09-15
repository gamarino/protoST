"EXPECT: 129"
"§6.3 + D29: a block inside an inner to:do: reads the outer loop's variable"
"and the inner loop's variable: 10i + j summed over i in 1..3, j in 1..2."
s := 0.
1 to: 3 do: [ :i | 1 to: 2 do: [ :j | s := s + [ i * 10 + j ] value ] ].
s.
