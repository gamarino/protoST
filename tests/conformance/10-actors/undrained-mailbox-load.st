"EXPECT: 1000"
"§10 + D23: a sender that fires many asynchronous sends at one actor
WITHOUT waiting on the returned Futures (the live-Future set grows
unbounded) must still drain every message and terminate. This is the
un-drained mailbox pattern that used to deadlock the actor scheduler
non-deterministically — see docs/STATUS.md D23. The repeated-launch
stress check lives in tests/cli/test_cli_actor_stress.sh; this single
case keeps the pattern in the conformance smoke layer."
Object subclass: #Sink instanceVariableNames: 'count'.
Sink >> initialize  count := 0. ^ self.
Sink >> ping  count := count + 1. ^ count.
Sink >> count  ^ count.
base := Sink new. base initialize.
sink := base asActor.
1 to: 1000 do: [ :i | sink ping ].
(sink count) wait.
