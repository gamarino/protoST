"EXPECT: 4"
"§13 stdlib: WriteStream << is a binary alias for nextPutAll: — it appends a
whole collection."
m := Import from: 'stream'.
ws := m WriteStream new.
ws << #(10 20).
ws << #(30 40).
ws size.
