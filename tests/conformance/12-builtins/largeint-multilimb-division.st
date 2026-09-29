"XFAIL: EXPECT: 1180591620717411303424 0 true"
"Known runtime bug (protoCore LargeInteger divide): when the divisor needs 64 bits or more and the quotient exceeds about 2^65, // (and /, quo:) answer 36893488147419103231 (2^65 - 1) and rem: answers a value larger than the divisor. Spec-correct: 2^140 // 2^70 = 2^70, remainder 0."
q := (2 raisedTo: 140) // (2 raisedTo: 70).
r := (2 raisedTo: 140) rem: (2 raisedTo: 70).
q printString , ' ' , r printString , ' ' , (q = (2 raisedTo: 70)) printString.
