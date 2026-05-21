"EXPECT: 1208925819614629174706176"
"§12.2 + D20: 2 raised to the 80th power, accumulated by repeated doubling,
stays an exact LargeInteger far past the SmallInteger range."
([ :p |
  | n acc |
  n := 0. acc := 1.
  [ n < p ] whileTrue: [ acc := acc * 2. n := n + 1 ].
  acc
] value: 80) printString.
