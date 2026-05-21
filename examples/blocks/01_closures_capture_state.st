"EXPECT: 30"
"-- 01_closures_capture_state.st -------------------------------------------"
"-- A closure captures the variables of the scope it is written in, by      --"
"-- reference. `makeAccumulator` returns a block holding a private `total`; --"
"-- each evaluation mutates that captured variable, which outlives the      --"
"-- method that created it — a genuine closure.                             --"
"-- Expected output: 30 (5 + 10 + 15)                                       --"

Object subclass: #Accumulators.

Accumulators >> makeAccumulator
  | total |
  total := 0.
  ^ [ :amount | total := total + amount. total ].

acc := Accumulators new makeAccumulator.
acc value: 5.
acc value: 10.
acc value: 15.
