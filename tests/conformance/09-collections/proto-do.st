"EXPECT: 6"
"§9.2: do: evaluates the block for each element"
sum := 0.
#(1 2 3) do: [ :e | sum := sum + e ].
sum.
