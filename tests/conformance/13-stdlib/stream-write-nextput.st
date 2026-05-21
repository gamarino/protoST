"EXPECT: 3"
"§13 stdlib: WriteStream nextPut: appends elements; contents yields an Array."
m := Import from: 'stream'.
ws := m WriteStream new.
ws nextPut: 1.
ws nextPut: 2.
c := ws contents.
(c at: 1) + (c at: 2).
