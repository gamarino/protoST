"multi_producer.st - Multi-producer message-throughput benchmark."
""
"NPROD driver actors each own a private slice of SINKS_PER_DRIVER sink"
"actors. Every driver is itself an actor: when sent #run it spins a"
"ROUNDS-iteration loop that fan-outs SENDs to each of its sinks, then"
"waits on the returned Futures. Drivers run independently — so up to"
"NPROD producers emit SEND traffic in parallel."
""
"With workers >= NPROD every driver gets a worker and produces at full"
"speed; the main thread is idle from kickoff onward. This isolates"
"worker scaling from main-thread producer rate (which is the cuello in"
"mt100a's single-producer pattern)."
""
"The driver method uses `doYielding:` (compiler-desugared yieldable"
"iteration) so the per-element `f wait` inside the loop parks the"
"driver cooperatively instead of blocking its worker."
""
"Total messages = NPROD * SINKS_PER_DRIVER * ROUNDS  (= 96 000 default)."

Object subclass: #Sink instanceVariableNames: 'count'.

Sink >> initialize
  count := 0.
  ^ self.

Sink >> ping
  count := count + 1.
  ^ count.

Object subclass: #Driver instanceVariableNames: 'sinks rounds'.

Driver >> initWith: ss r: n
  sinks := ss.
  rounds := n.
  ^ self.

Driver >> run
  | total futures roundsList |
  total := 0.
  roundsList := OrderedCollection new.
  "Build the rounds list FIRST (no wait inside; the regular `do:` here is
   safe because the block does not yield). Then iterate it with doYielding:
   so the outer loop is yieldable too — required because each iteration's
   inner `f wait` will yield, and the outer to:do: primitive's C++ stack
   state cannot survive a yield. doYielding: lives entirely in bytecode."
  1 to: rounds do: [ :r | roundsList add: r ].
  roundsList doYielding: [ :r |
    futures := OrderedCollection new.
    sinks doYielding: [ :s | futures add: (s ping) ].
    futures doYielding: [ :f | total := total + f wait ] ].
  ^ total.

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
  drv initWith: slice r: 1000.
  drivers add: (drv asActor) ].

futures := OrderedCollection new.
drivers do: [ :d | futures add: (d run) ].

sum := 0.
futures do: [ :f | sum := sum + f wait ].
sum.
