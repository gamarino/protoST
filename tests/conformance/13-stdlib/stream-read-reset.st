"EXPECT: 5"
"§13 stdlib: ReadStream reset rewinds the cursor to the start."
m := Import from: 'stream'.
rs := m ReadStream on: #(5 6 7).
rs next.
rs next.
rs reset.
rs next.
