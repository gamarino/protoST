"EXPECT: 20"
"§13 stdlib: ReadStream next/peek/atEnd over an Array, imported from the
standard library with no STPATH set — proving default lib/ discovery."
m := Import from: 'stream'.
rs := m ReadStream on: #(10 20 30).
rs next.
rs peek.
