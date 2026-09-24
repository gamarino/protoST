"mailbox_send_headroom.st — a large undrained mailbox backlog."
""
"Same shape as mailbox_send_cost.st, two orders of magnitude bigger: 200 000"
"messages are enqueued with the worker pool stopped and then drained."
""
"This case exists to be run on BOTH sides of the ProtoMPSCQueue migration,"
"because the two sides do not merely differ by a constant. With the immutable"
"ProtoList mailbox a pop rebuilt the whole list, so a backlog of n messages"
"drained in O(n^2) time and space; on this machine 5000 messages already cost"
"12 s and 10.7 GB of resident memory and 10 000 was killed by earlyoom, so"
"200 000 could not be reached at all. A build that cannot finish this file is"
"the result, not an error in the file — the runner records it as such."
""
"It is opt-in (WITH_HEADROOM=1) precisely because the pre-migration build is"
"expected to be killed, and a benchmark that is expected to die must never run"
"by accident next to other people's work on a shared machine."
""
"Machine-readable output, one key per line:"
"  SENDS <n>         messages enqueued while the pool was stopped"
"  ELAPSED_US <us>   wall clock of the enqueue loop alone"
"  DRAIN_US <us>     wall clock of draining those messages afterwards"
"  VERIFIED <n>      messages the sink actually processed (must equal SENDS)"
"  EXPECTED <n>      what VERIFIED has to be"

Object subclass: #Sink instanceVariableNames: 'count'.

Sink >> initialize
  count := 0.
  ^ self.

Sink >> tick
  count := count + 1.
  ^ count.

Sink >> total
  ^ count.

sends := 200000.

base := Sink new.
base initialize.
sink := base asActor.

WorkerPool stopProcessing.

t0 := Object new __monotonicMicros.
1 to: sends do: [ :i | sink tick ].
t1 := Object new __monotonicMicros.

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
