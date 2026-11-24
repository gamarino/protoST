"fraction_new.st - Benchmark: 300000 Fraction constructions through"
"Fraction class >> numerator:denominator:, each folded into the result by a"
"read of the new instance (explicit-state spec 14.1, non-regression of the hot"
"constructors: 0.17.0 ends numerator:denominator: with the"
"__checkConstructed:selector: primitive)."
"Result: 600000  (300000 * 2)."

Object subclass: #FractionBench instanceVariableNames: ''.

FractionBench >> run: n
  | total |
  total := 0.
  1 to: n do: [ :i | total := total + (Fraction numerator: 1 denominator: 2) denominator ].
  ^ total.

result := FractionBench new run: 300000.
result = 600000 ifFalse: [Error signal: 'fraction_new: wrong result ' , result printString].
Transcript showCr: 'VERIFIED ' , result printString.
