"int_sum_loop.st - Benchmark: sum 1..N with N=100000."
"Direct counterpart of benchmarks/int_sum_loop.py (sum(range(100000)))."
"Result: 5000050000."

result := (0 to: 99999) inject: 0 into: [ :acc :each | acc + each ].
result = 4999950000 ifFalse: [Error signal: 'int_sum_loop: wrong result ' , result printString].
'VERIFIED ' , result printString

