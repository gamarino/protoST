"EXPECT: 6"
"§6.4: to:by:do: counts down with a negative step (3,2,1 -> 6)"
sum := 0.
3 to: 1 by: 0 - 1 do: [ :i | sum := sum + i ].
sum.
