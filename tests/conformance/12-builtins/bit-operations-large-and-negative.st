"EXPECT: #(-1180591620717411303423 1180591620717411303424 -1 1180591620717411303427 -1180591620717411303425 -1180591620717411303424 3)"
"Bitwise and/or/xor follow two's complement for LargeIntegers and negative
 operands together (they answered wrong positive values)."
| big |
big := 2 raisedTo: 70.
{ big negated bitOr: 1. big bitAnd: -1. big bitOr: -1. -5 bitAnd: big + 7.
  big bitXor: -1. big negated bitAnd: -1. -5 bitAnd: 7 }
