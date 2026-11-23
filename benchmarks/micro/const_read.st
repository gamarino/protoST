"const_read.st - Benchmark: 100000 iterations of 3 constant instance-variable reads + sums."
"The exact twin of benchmarks/comparable/attr_lookup.st, with the three variables"
"declared constantNames: and set by the generated constructor (explicit-state"
"spec 14.1). Each read checks that the constant is set; the difference between"
"the two programs is that check. protoST 0.16.1 cannot parse it."
"Result: 600000  (100000 * (1 + 2 + 3))."

Object subclass: #FastObject constantNames: 'a b c'.

FastObject >> runBench: n
  | total |
  total := 0.
  1 to: n do: [ :i |
    total := total + a.
    total := total + b.
    total := total + c ].
  ^ total.

obj := FastObject a: 1 b: 2 c: 3.
result := obj runBench: 100000.
result = 600000 ifFalse: [Error signal: 'const_read: wrong result ' , result printString].
Transcript showCr: 'VERIFIED ' , result printString.
