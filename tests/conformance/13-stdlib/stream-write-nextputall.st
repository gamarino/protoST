"EXPECT: 24"
"§13 stdlib: WriteStream nextPutAll: appends every element of a collection."
m := Import from: 'stream'.
ws := m WriteStream new.
ws nextPutAll: #(7 8 9).
c := ws contents.
(c at: 1) + (c at: 2) + (c at: 3).
