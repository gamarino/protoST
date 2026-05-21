"EXPECT: 12"
"§13 stdlib: ReadStream contents returns the whole underlying collection,
independent of the cursor position."
m := Import from: 'stream'.
rs := m ReadStream on: #(3 4 5).
rs next.
rs next.
all := rs contents.
(all at: 1) + (all at: 2) + (all at: 3).
