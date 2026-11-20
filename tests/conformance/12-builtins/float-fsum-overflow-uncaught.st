"EXPECT-ERROR: fsum: intermediate overflow"
"An exact sum beyond the Float range signals an Error that, uncaught, ends the program (CPython's OverflowError)."
Float fsum: #(1.0e308 1.0e308 -1.0e308).
