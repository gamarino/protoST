"message_throughput.st - Actor-model benchmark: mailbox message throughput."
""
"N messages are sent to a single actor's mailbox and the result of each is"
"awaited before the next is sent. The actor processes them one at a time -- the"
"single-method invariant -- so this measures the full round-trip cost of a"
"message: enqueue, schedule, execute, settle the Future, wake the sender. The"
"harness divides the message count by the wall-clock time to report"
"messages/second."
""
"Why each send is drained with `wait`: every send returns a Future, and a"
"fire-and-forget sender never consumes them, so the live-Future set grows with"
"N. Past a few thousand un-drained futures the current scheduler degrades"
"sharply and non-deterministically (a known runtime issue -- see the"
"2026-05-21 report and docs/STATUS.md). Draining each message bounds the"
"outstanding-Future set to one and keeps the run stable."
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
