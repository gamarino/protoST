"saturation_big.st - Larger saturation benchmark for ~ 5-7s w=1."
""
"32 actors x 50 msgs = 1600 messages. Each work() runs 5000 iterations"
"of a captured-variable sum (~ 25 ms of CPU each). 10x the original"
"saturation.st but bounded by allocation rate — each work() invocation"
"creates a fresh captured dict and ~ 15K cells of attribute COW state."
""
"NACTORS > NWORKERS so workers steal across actors. Use the worker-"
"stats env var to confirm balance:"
""
"  systemd-run --user --scope -p MemoryMax=8G --quiet env "
"    PROTOST_WORKERS=N PROTOST_WORKER_STATS=1 "
"    ./build/protost benchmarks/actors/saturation_big.st"
""
"For perf record use a smaller cap if MaxRSS is comfortably bounded."

Object subclass: #Worker instanceVariableNames: 'count'.

Worker >> initialize
  count := 0.
  ^ self.

Worker >> work
  | sum |
  sum := 0.
  1 to: 5000 do: [ :i | sum := sum + i ].
  count := count + 1.
  ^ sum.

actors := OrderedCollection new.
1 to: 32 do: [ :i |
  | w |
  w := Worker new.
  w initialize.
  actors add: (w asActor) ].

WorkerPool stopProcessing.

futures := OrderedCollection new.
1 to: 50 do: [ :m |
  actors do: [ :a | futures add: (a work) ] ].

t0 := Object new __monotonicMillis.
WorkerPool startProcessing.
futures do: [ :f | f wait ].
t1 := Object new __monotonicMillis.

(t1 - t0) printNl.
futures size.
