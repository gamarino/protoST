"EXPECT: 1180591620717411303424 0 true"
"LargeInteger division with a divisor of 64 bits or more is exact (protoCore's divide answered 2^65 - 1 for 2^140 // 2^70; protoST divides such operands itself)."
q := (2 raisedTo: 140) // (2 raisedTo: 70).
r := (2 raisedTo: 140) rem: (2 raisedTo: 70).
q printString , ' ' , r printString , ' ' , (q = (2 raisedTo: 70)) printString.
