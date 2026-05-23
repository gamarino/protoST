"saturation.st - Worker-pool capacity benchmark."
""
"Isolates the actor pool's pure drain capacity from the main-thread"
"producer rate. We"
"  1) pause the pool (WorkerPool stopProcessing);"
"  2) pre-fill every actor's mailbox from main while paused — workers"
"     stay idle, every SEND just enqueues + records a Future;"
"  3) snapshot t0; release the pool (WorkerPool startProcessing); wait"
"     until every pre-loaded Future has settled; snapshot t1;"
"  4) report the elapsed milliseconds."
""
"Under this shape main does no work between t0 and t1 except waiting"
"on Futures, so the wall time reflects the pool draining"
"NACTORS * NMSGS already-enqueued messages. Worker scaling now"
"measures the pool's CPU capacity, not a producer-bounded rate."
""
"ALWAYS run under a cgroup cap. The pre-loaded mailbox + the work"
"allocations push the heap a few hundred MB; a 2 GB cap pressures GC"
"into constant STW (we measured this artefact earlier), but anything"
"≥ 8 GB lets the heap breathe. An uncapped run is dangerous because"
"the worker burst-allocation pattern can outrun earlyoom and crash"
"the host (lesson learned 2026-05-23 night)."
""
"  systemd-run --user --scope -p MemoryMax=12G --quiet env "
"    PROTOST_WORKERS={1,2,4,8} ./build/protost benchmarks/actors/saturation.st"

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
1 to: 16 do: [ :i |
  | w |
  w := Worker new.
  w initialize.
  actors add: (w asActor) ].

"Pause BEFORE loading any mailboxes."
WorkerPool stopProcessing.

"Pre-load: 16 actors x 10 msgs = 160 messages. Each work() runs"
"5 000 iterations of a captured-variable sum (~ 20-30 ms of CPU)."
"Total single-thread budget ~ 4 s, 8-worker ideal ~ 500 ms. With"
"NACTORS >> NWORKERS, workers steal across actors rather than each"
"binding to one — exercises the ready-queue / actor-lock path. Each"
"SEND returns a Future synchronously; the work block runs only after"
"startProcessing wakes the pool."
futures := OrderedCollection new.
1 to: 10 do: [ :m |
  actors do: [ :a | futures add: (a work) ] ].

"Snapshot t0, release the pool, wait for every future, snapshot t1."
t0 := Object new __monotonicMillis.
WorkerPool startProcessing.
futures do: [ :f | f wait ].
t1 := Object new __monotonicMillis.

"Print elapsed ms (pure drain phase)."
(t1 - t0) printNl.
futures size.
