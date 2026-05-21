"EXPECT: true"
"§13 stdlib: ReadStream atEnd is true once every element has been read."
m := Import from: 'stream'.
rs := m ReadStream on: #(8 9).
rs next.
rs next.
rs atEnd.
