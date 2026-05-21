"EXPECT: 9"
"§13 stdlib: ReadStream next: n returns an Array of the next n elements,
advancing the cursor."
m := Import from: 'stream'.
rs := m ReadStream on: #(2 3 4 5 6).
batch := rs next: 3.
(batch at: 1) + (batch at: 2) + (batch at: 3).
