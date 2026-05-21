"int_sum_loop.st - Benchmark: sum 1..N with N=100000."
"Direct counterpart of benchmarks/int_sum_loop.py (sum(range(100000)))."
"Result: 5000050000."

result := (1 to: 100000) inject: 0 into: [ :acc :each | acc + each ].
result.
