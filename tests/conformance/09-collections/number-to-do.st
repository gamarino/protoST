"EXPECT: 15"
"§12.2: Number>>to:do: iterates receiver..stop"
sum := 0.
1 to: 5 do: [ :i | sum := sum + i ].
sum.
