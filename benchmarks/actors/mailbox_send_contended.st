"mailbox_send_contended.st — cost of an actor mailbox send under contention."
""
"Eight producer actors push into ONE sink actor's mailbox at the same time,"
"which is the multi-producer / single-consumer shape the mailbox exists for."
"The measured interval ends when the last producer's own Future settles, i.e."
"when every send has been enqueued; the sink drains concurrently, so the"
"number is an honest end-to-end enqueue cost under load rather than a"
"contention-free micro-figure. Sweep PROTOST_WORKERS to see the scaling."
""
"Producers do not await the Futures they create: awaiting would serialise the"
"producers and measure a round trip instead of a send."
""
"The benchmark reports and verifies its work twice over: each producer answers"
"how many sends it made, and the sink answers its own processed count after"
"every message has drained. A mismatch prints the wrong number and the runner"
"fails the run."
""
"Machine-readable output, one key per line:"
"  PRODUCERS <n>      concurrent producer actors"
"  PER_PRODUCER <n>   sends each producer makes"
"  SENDS <n>          PRODUCERS * PER_PRODUCER"
"  ELAPSED_US <us>    wall clock until the last send was enqueued"
"  TOTAL_US <us>      wall clock until the sink had drained everything"
"  VERIFIED <n>       messages the sink processed (must equal SENDS)"
"  EXPECTED <n>       what VERIFIED has to be"
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

Object subclass: #Producer instanceVariableNames: 'target rounds'.

Producer >> to: anActor rounds: n
  target := anActor.
  rounds := n.
  ^ self.

Producer >> run
  1 to: rounds do: [ :i | target tick ].
  ^ rounds.

"8 x 250 is small for the same reason mailbox_send_cost.st uses 2000: the"
"ProtoList mailbox drains an n-message backlog in O(n^2) time and space"
"(8 x 500 = 4000 messages already cost 7 s and 6.7 GB on this machine), so the"
"pre-migration build sets the size at which the comparison can be made."
producerCount := 8.
perProducer := 250.
sends := producerCount * perProducer.

base := Sink new.
base initialize.
sink := base asActor.

producers := OrderedCollection new.
1 to: producerCount do: [ :i |
  | p |
  p := Producer new.
  p to: sink rounds: perProducer.
  producers add: (p asActor) ].

t0 := Object new __monotonicMicros.
futures := OrderedCollection new.
producers do: [ :p | futures add: (p run) ].
enqueued := 0.
futures do: [ :f | enqueued := enqueued + f wait ].
t1 := Object new __monotonicMicros.

"Every send is now in the sink's mailbox or already processed. This last send"
"is behind all of them in FIFO order, so its answer is the final count."
processed := (sink total) wait.
t2 := Object new __monotonicMicros.

('PRODUCERS ' , producerCount printString) printNl.
('PER_PRODUCER ' , perProducer printString) printNl.
('SENDS ' , enqueued printString) printNl.
('ELAPSED_US ' , (t1 - t0) printString) printNl.
('TOTAL_US ' , (t2 - t0) printString) printNl.
('VERIFIED ' , processed printString) printNl.
('EXPECTED ' , sends printString) printNl.
processed.
