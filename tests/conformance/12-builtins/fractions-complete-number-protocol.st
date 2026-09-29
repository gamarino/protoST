"EXPECT: #((3/2) (3/2) (1/2) true false (1/5) 6 (1/3) 2.0 (1/4) (8/27) 0.5 false 1 -1 (4/9) (2/3) 0 (3/2) (5/2))"
"Fractions answer the Number protocol exactly (or through asFloat where the
 result is irrational), as receiver and as argument of native numbers."
{ (3/2) max: 1. 1 max: (3/2). (1/2) min: (2/3).
  (1/2) between: 0 and: 1. (3/2) between: 0 and: 1.
  5 reciprocal. 3 // (1/2). 3 \\ (2/3). 4 raisedTo: 1/2.
  (1/2) squared. (2/3) raisedTo: 3. (1/4) sqrt.
  (1/2) isZero. (1/2) sign. (-1/2) sign. (3/2) raisedTo: -2.
  #(1 2 3) inject: 0 into: [:m :i | m max: 2/3].
  (1/3) truncated. (-3/2) abs. (12/5) roundTo: 1/2 }
