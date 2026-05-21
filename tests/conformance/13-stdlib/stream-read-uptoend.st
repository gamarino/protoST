"EXPECT: 7"
"§13 stdlib: ReadStream upToEnd returns the remaining elements after the
cursor as an Array."
m := Import from: 'stream'.
rs := m ReadStream on: #(1 3 4).
rs next.
rest := rs upToEnd.
(rest at: 1) + (rest at: 2).
