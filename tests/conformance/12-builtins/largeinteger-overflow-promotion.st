"EXPECT: 15511210043330985984000000"
"§12.2 + D20: an integer computation that overflows the 56-bit SmallInteger
range promotes transparently to an exact LargeInteger. 25! (~1.5e25) is far
past 2^56, computed here with a whileTrue: loop; the exact value must print."
([ :limit |
  | n f |
  n := 1. f := 1.
  [ n <= limit ] whileTrue: [ f := f * n. n := n + 1 ].
  f
] value: 25) printString.
