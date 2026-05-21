"fib.st - Benchmark: fib(25) computed recursively."
"Direct counterpart of benchmarks/call_recursion.py (fib(25))."
"Result: 75025."

Object subclass: #Fib.

Fib >> compute: n
  (n <= 1) ifTrue: [ ^ n ].
  ^ (self compute: n - 1) + (self compute: n - 2).

f := Fib new.
f compute: 25.
