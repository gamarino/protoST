"EXPECT: 49"
"-- 02_future_thendo.st ----------------------------------------------------"
"-- A Future is a first-class value. `Future new` makes a manually-settled  --"
"-- promise; `resolve:` settles it and `wait` reads it. `thenDo:` registers --"
"-- a callback that runs with the resolved value. Here a worker actor       --"
"-- computes a square asynchronously and we `wait` for the result.          --"
"-- Expected output: 49                                                     --"

Object subclass: #Worker.
Worker >> square: n
  ^ n * n.

w := Worker new asActor.
future := w square: 7.

"thenDo: schedules a side-effecting callback when the future resolves."
future thenDo: [ :result | result printNl ].

"wait gives back the resolved value as the program's result."
future wait.
