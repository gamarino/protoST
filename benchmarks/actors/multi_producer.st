"multi_producer.st - Multi-producer message-throughput benchmark."
""
"Goal: measure how throughput scales with worker count when SENDs are"
"emitted from MANY actors in parallel (not from a single main thread)."
"This is the missing companion to mt100a, which only has main as the"
"sender — and so saturates main long before workers run out of room."
""
"Structure:"
"  * 8 driver actors. Each owns 12 private sink actors."
"  * Each round, main fan-outs `roundOnce` to every driver and waits"
"    for the eight returned futures. The eight drivers therefore run"
"    in parallel on the worker pool; up to 8 producers are emitting"
"    SEND traffic to their sinks simultaneously."
"  * `Driver >> roundOnce` is hand-unrolled (12 send/wait pairs in"
"    method body, not a `do:` loop). protoST's current runtime cannot"
"    yield from inside a `do:` block in an actor method (see"
"    docs/superpowers/specs/2026-05-23-multiproducer-blocker.md and the"
"    coalesce-snapshot fix in ExecutionEngine.cpp); inline send + inline"
"    wait stay in the actor's outer engine and yield correctly."
"  * Main's `do:` loops are fine — main is not an actor; its own engine"
"    blocks on `f wait` rather than yielding."
""
"Total messages = 8 * 12 * 1000 = 96 000."
""
"Run:  PROTOST_WORKERS={1,2,4,8} protost multi_producer.st"
"Expected result: 96 000."

Object subclass: #Sink instanceVariableNames: 'count'.

Sink >> initialize
  count := 0.
  ^ self.

Sink >> ping
  count := count + 1.
  ^ count.

Object subclass: #Driver instanceVariableNames: 'sinks'.

Driver >> setSinks: ss
  sinks := ss.
  ^ self.

Driver >> roundOnce
  | f1 f2 f3 f4 f5 f6 f7 f8 f9 f10 f11 f12 |
  f1  := (sinks at: 1)  ping.
  f2  := (sinks at: 2)  ping.
  f3  := (sinks at: 3)  ping.
  f4  := (sinks at: 4)  ping.
  f5  := (sinks at: 5)  ping.
  f6  := (sinks at: 6)  ping.
  f7  := (sinks at: 7)  ping.
  f8  := (sinks at: 8)  ping.
  f9  := (sinks at: 9)  ping.
  f10 := (sinks at: 10) ping.
  f11 := (sinks at: 11) ping.
  f12 := (sinks at: 12) ping.
  f1 wait.
  f2 wait.
  f3 wait.
  f4 wait.
  f5 wait.
  f6 wait.
  f7 wait.
  f8 wait.
  f9 wait.
  f10 wait.
  f11 wait.
  f12 wait.
  ^ 12.

drivers := OrderedCollection new.
1 to: 8 do: [ :d |
  | slice drv |
  slice := OrderedCollection new.
  1 to: 12 do: [ :i |
    | s |
    s := Sink new.
    s initialize.
    slice add: (s asActor) ].
  drv := Driver new.
  drv setSinks: slice.
  drivers add: (drv asActor) ].

total := 0.
1 to: 1000 do: [ :r |
  | futures |
  futures := OrderedCollection new.
  drivers do: [ :d | futures add: (d roundOnce) ].
  futures do: [ :f | total := total + f wait ] ].

total.
