"EXPECT: 10"
"§13 stdlib: ReadStream do: iterates the remaining elements."
m := Import from: 'stream'.
rs := m ReadStream on: #(1 2 3 4).
sum := 0.
rs do: [ :e | sum := sum + e ].
sum.
