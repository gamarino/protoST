"mailbox_send_cost.st — uncontended cost of one actor mailbox send."
""
"The worker pool is stopped, so nothing drains while the sends happen: the"
"measured interval contains the enqueue path and nothing else. That makes it"
"the cleanest comparison between mailbox representations (the immutable"
"ProtoList updated with setAttributeIfEqual versus protoCore's"
"ProtoMPSCQueue), because a drain running concurrently would otherwise"
"dominate the wall clock."
""
"The benchmark reports the work it did and verifies it: after the pool is"
"restarted, one further send answers the sink's own counter, and a run whose"
"counter does not equal the number of sends prints VERIFIED with the wrong"
"number so the runner fails it. A crash that exits 0 cannot look like a win."
""
"Machine-readable output, one key per line:"
"  SENDS <n>         messages enqueued while the pool was stopped"
"  ELAPSED_US <us>   wall clock of the enqueue loop alone"
"  DRAIN_US <us>     wall clock of draining those messages afterwards"
"  VERIFIED <n>      messages the sink actually processed (must equal SENDS)"
"  EXPECTED <n>      what VERIFIED has to be"
""
"Invocation: PROTOST_BIN=./build_release/protost benchmarks/actors/run_mailbox_bench.sh"

Object subclass: #Sink instanceVariableNames: 'count'.

Sink >> initialize
  count := 0.
  ^ self.

Sink >> tick
  count := count + 1.
  ^ count.

Sink >> total
  ^ count.

"2000 is deliberately small, and the reason is a finding in its own right:"
"with the ProtoList mailbox a pop rebuilds the whole list, so draining an"
"undrained backlog of n messages costs O(n^2). Measured on this machine before"
"the ProtoMPSCQueue migration: 2000 messages drain in 1.6 s at 1.6 GB RSS,"
"5000 in 12 s at 10.7 GB, and 10000 is killed by earlyoom. The size therefore"
"has to fit the slower of the two representations for the comparison to exist"
"at all. mailbox_send_headroom.st is the large-backlog case."
sends := 2000.

base := Sink new.
base initialize.
sink := base asActor.

"Stop the drain before the first send, so the interval below is pure enqueue."
WorkerPool stopProcessing.

t0 := Object new __monotonicMicros.
1 to: sends do: [ :i | sink tick ].
t1 := Object new __monotonicMicros.

"Restart the pool and drain. The `total` send is enqueued last, so per-actor"
"FIFO guarantees it runs after every tick — its answer is the true count."
t2 := Object new __monotonicMicros.
WorkerPool startProcessing.
processed := (sink total) wait.
t3 := Object new __monotonicMicros.

('SENDS ' , sends printString) printNl.
('ELAPSED_US ' , (t1 - t0) printString) printNl.
('DRAIN_US ' , (t3 - t2) printString) printNl.
('VERIFIED ' , processed printString) printNl.
('EXPECTED ' , sends printString) printNl.
processed.
