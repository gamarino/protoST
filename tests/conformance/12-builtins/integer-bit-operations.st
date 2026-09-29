"EXPECT: #(8 14 6 -13 40 3 1 0 true false 1267650600228229401496703205376 true 32)"
"Integers answer the bitwise protocol, LargeIntegers included."
{ 12 bitAnd: 10. 12 bitOr: 10. 12 bitXor: 10. 12 bitInvert.
  5 bitShift: 3. 12 bitShift: -2. 5 bitAt: 1. 5 bitAt: 2.
  64 isPowerOfTwo. 65 isPowerOfTwo. 1 bitShift: 100.
  ((1 bitShift: 100) bitAnd: (1 bitShift: 100)) = (1 bitShift: 100).
  2 ** 5 }
