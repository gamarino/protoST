"exception_latency.st - Benchmark: 50000 iterations, signal+on:do: on even i."
"Direct counterpart of benchmarks/exception_latency.py."
"On even i an Error is signalled and caught; on odd i the body runs normally."
"Result: 50000  (the protected block contributes 1 on every iteration)."

total := 0.
1 to: 50000 do: [ :i |
  [ (i \\ 2 = 0) ifTrue: [ Error new signal: 'even' ].
    total := total + 1 ]
  on: Error
  do: [ :e | total := total + 1 ] ].
total.
