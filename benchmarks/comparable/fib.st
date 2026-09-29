"fib.st - Benchmark: fib(25) computed recursively."
"Direct counterpart of benchmarks/call_recursion.py (fib(25))."
"Result: 75025."

Object subclass: #Fib.

Fib >> compute: n
  (n <= 1) ifTrue: [ ^ n ].
  ^ (self compute: n - 1) + (self compute: n - 2).

f := Fib new.
result := f compute: 25.
result = 75025 ifFalse: [Error signal: 'fib: wrong result ' , result printString].
Transcript showCr: 'VERIFIED ' , result printString.
