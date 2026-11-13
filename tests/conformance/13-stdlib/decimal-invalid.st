"EXPECT: 4 refused"
"§13 stdlib: Decimal fromString: signals Error on text that is not a decimal
 string: '1..2', '', '1e' and 'NaNx'."
| m n |
m := Import from: 'decimal'.
n := 0.
#('1..2' '' '1e' 'NaNx') do: [:t |
    [m Decimal fromString: t] on: Error do: [:e | n := n + 1]].
n printString , ' refused'.
