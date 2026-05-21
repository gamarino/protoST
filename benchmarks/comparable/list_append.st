"list_append.st - Benchmark: append N=10000 items to an OrderedCollection."
"Direct counterpart of benchmarks/list_append_loop.py."
"Result: 10000 (the final collection size)."

c := OrderedCollection new.
1 to: 10000 do: [ :i | c add: i ].
c size.
