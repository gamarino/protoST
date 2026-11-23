"point_new.st - Benchmark: 300000 Point constructions through Point class >> x:y:,"
"each folded into the result by a read of the new instance (explicit-state"
"spec 14.1, non-regression of the hot constructors: 0.17.0 ends x:y: with the"
"__checkConstructed:selector: primitive)."
"Result: 600000  (300000 * 2)."

Object subclass: #PointBench instanceVariableNames: ''.

PointBench >> run: n
  | total |
  total := 0.
  1 to: n do: [ :i | total := total + (Point x: i y: 2) y ].
  ^ total.

result := PointBench new run: 300000.
result = 600000 ifFalse: [Error signal: 'point_new: wrong result ' , result printString].
Transcript showCr: 'VERIFIED ' , result printString.
