"attr_lookup.st - Benchmark: 100000 iterations of 3 instance-variable reads + sums."
"Direct counterpart of benchmarks/attr_lookup.py."
"Result: 600000  (100000 * (1 + 2 + 3))."

Object subclass: #FastObject instanceVariableNames: 'a b c'.

FastObject >> setA: x b: y c: z
  a := x. b := y. c := z.
  ^ self.

FastObject >> runBench: n
  total := 0.
  1 to: n do: [ :i |
    total := total + a.
    total := total + b.
    total := total + c ].
  ^ total.

obj := FastObject new.
obj setA: 1 b: 2 c: 3.
obj runBench: 100000.
