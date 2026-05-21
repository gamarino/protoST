"EXPECT: 142"
"-- 04_pipeline.st ---------------------------------------------------------"
"-- An actor pipeline. Three stages, each its own actor: `Doubler`,         --"
"-- `Incrementer`, `Squarer`. An item flows stage-to-stage; each stage's    --"
"-- output Future is `wait`ed and fed into the next. The driver pushes one  --"
"-- value through the whole pipeline.                                       --"
"--                                                                        --"
"-- For input 5: double -> 10, increment -> 11, square -> 121. Summing the  --"
"-- per-stage trace (10 + 11 + 121) gives a deterministic 142.              --"
"-- Expected output: 142                                                    --"

Object subclass: #Doubler.
Doubler >> apply: n
  ^ n * 2.

Object subclass: #Incrementer.
Incrementer >> apply: n
  ^ n + 1.

Object subclass: #Squarer.
Squarer >> apply: n
  ^ n * n.

stage1 := Doubler new asActor.
stage2 := Incrementer new asActor.
stage3 := Squarer new asActor.

"Push 5 through: 5 -> 10 -> 11 -> 121."
a := (stage1 apply: 5) wait.
b := (stage2 apply: a) wait.
c := (stage3 apply: b) wait.

"Sum the intermediate trace: 10 + 11 + 121 = 142."
a + b + c.
