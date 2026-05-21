"XFAIL: EXPECT: a Float"
"§12.2 + D11: Float arithmetic is not separately bound. Spec-correct: a float
arithmetic send should produce a Float; currently it is a doesNotUnderstand."
(3.14 + 1.0) printString.
