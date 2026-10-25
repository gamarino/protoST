| sum i |
sum := 0.
1 to: 10 do: [ :k | sum := sum + k ].
i := 0.
[ i < 3 ] whileTrue: [ i := i + 1 ].
(sum + i) printNl.
