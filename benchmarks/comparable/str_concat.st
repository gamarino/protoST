"str_concat.st - Benchmark: N=2000 string concatenations with `,`."
"Direct counterpart of benchmarks/str_concat_loop.py."
"N is kept moderate because each concat is O(N) so the loop is O(N^2)."
"Result: 2000 (the final string length)."

s := ''.
1 to: 2000 do: [ :i | s := s , 'x' ].
s size.
