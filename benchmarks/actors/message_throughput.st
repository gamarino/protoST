"message_throughput.st - Actor-model benchmark: mailbox message throughput."
""
"N messages are sent to a single actor's mailbox and the result of each is"
"awaited before the next is sent. The actor processes them one at a time -- the"
"single-method invariant -- so this measures the full round-trip cost of a"
"message: enqueue, schedule, execute, settle the Future, wake the sender. The"
"harness divides the message count by the wall-clock time to report"
"messages/second."
""
"Why each send is drained with `wait`: it measures the full round-trip cost"
"of a message rather than just the enqueue cost. Draining bounds the"
"outstanding-Future set to one. (The un-drained pattern is also correct now"
"that D23 -- the scheduler deadlock under sustained mailbox load -- is fixed;"
"see docs/STATUS.md. This benchmark stays drained so it times round trips.)"
""
"Result: 2000  (the final mailbox-processed count)."

Object subclass: #Sink instanceVariableNames: 'count'.

Sink >> initialize
  count := 0.
  ^ self.

Sink >> ping
  count := count + 1.
  ^ count.

base := Sink new.
base initialize.
sink := base asActor.

last := 0.
1 to: 2000 do: [ :i | last := (sink ping) wait ].
last.
