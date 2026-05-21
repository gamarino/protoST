"range_iterate.st - Benchmark: iterate 1..N counting, N=100000."
"Direct counterpart of benchmarks/range_iterate.py."
"Result: 100000."

n := 0.
1 to: 100000 do: [ :i | n := n + 1 ].
n.
