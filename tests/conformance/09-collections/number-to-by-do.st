"EXPECT: 9"
"§12.2: Number>>to:by:do: iterates with a step (1,3,5 -> 9)"
sum := 0.
1 to: 5 by: 2 do: [ :i | sum := sum + i ].
sum.
