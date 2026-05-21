"EXPECT: 285"
"-- 03_fan_out_fan_in.st ---------------------------------------------------"
"-- The fan-out / fan-in pattern. A coordinator fans a batch of work out    --"
"-- to several worker actors at once (every send returns a Future without   --"
"-- blocking), then fans the results back in by `wait`ing on each Future.   --"
"-- Because the workers are distinct actors, the runtime can schedule them  --"
"-- on different worker threads in parallel.                                --"
"-- Expected output: 285  (sum of squares 1..9)                             --"

Object subclass: #SquareWorker.
SquareWorker >> square: n
  ^ n * n.

"Fan out: nine independent worker actors, one job each."
results := OrderedCollection new.
futures := OrderedCollection new.
1 to: 9 do: [ :n |
  | worker |
  worker := SquareWorker new asActor.
  futures add: (worker square: n) ].

"Fan in: collect every result by waiting on its Future."
futures do: [ :f | results add: f wait ].

results inject: 0 into: [ :acc :each | acc + each ].
