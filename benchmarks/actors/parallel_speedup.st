"parallel_speedup.st - Actor-model benchmark: parallel CPU speedup."
""
"Twelve independent worker actors each run a CPU-bound loop. Every `burn:`"
"send returns a Future immediately, so the twelve jobs are in flight at once"
"and the runtime schedules them across the whole worker pool."
""
"The harness runs this file twice -- once with the full worker pool and once"
"with PROTOST_WORKERS=1 -- and reports the wall-clock speedup. protoPython"
"has no actor model, so there is no Python counterpart: this is protoST's"
"distinctive feature."
""
"Result: 135000900000  (12 * sum(1..150000))."

Object subclass: #CpuWorker.

CpuWorker >> burn: rounds
  total := 0.
  1 to: rounds do: [ :i | total := total + i ].
  ^ total.

futures := OrderedCollection new.
1 to: 12 do: [ :w |
  futures add: ((CpuWorker new asActor) burn: 150000) ].

sum := 0.
futures do: [ :f | sum := sum + f wait ].
sum.
