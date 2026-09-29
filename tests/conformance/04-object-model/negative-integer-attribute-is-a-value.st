"EXPECT: -3"
"A negative integer stored as an attribute is a value, not a primitive marker
 (bit 62 is set in its two's complement). Reading it through a unary send used to
 index the primitive table with a negative number and abort the interpreter."
(10 to: 1 by: -3) __interval_step__
