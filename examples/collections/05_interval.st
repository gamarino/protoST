"EXPECT: 2550"
"-- 05_interval.st ---------------------------------------------------------"
"-- An Interval is a lazy arithmetic sequence — it stores start/stop/step   --"
"-- and computes elements on demand, with no backing store. `1 to: 100` is  --"
"-- an Interval; it answers the full iteration protocol, here `inject:into:`--"
"-- to sum the first hundred integers.                                      --"
"-- Expected output: 2550  (the sum of every even number in 1..100)         --"

(1 to: 100)
  inject: 0
  into: [ :acc :n | n isEven ifTrue: [ acc + n ] ifFalse: [ acc ] ].
