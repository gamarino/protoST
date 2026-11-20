"EXPECT-ERROR: fsum: intermediate overflow"
"An exact sum of squares beyond the Float range signals an Error that, uncaught, ends the program (CPython's OverflowError from math.fsum)."
Float fsumOfPowSquaredDifferences: #(1.0e154 1.0e154 1.0) with: #(0.0 0.0 0.0).
