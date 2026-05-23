"EXPECT: 3"
"Regression test for the doYielding: + multi-worker race fixed in
 commit (this one). Reproduces the deadlock: driver actor uses
 doYielding: with `wait` inside the block; sinks settle the
 future BEFORE the driver's wait can register as a waiter. Without
 the fix, finishDrain treats the stale-wakeup state=2 as if the
 actor were not suspended and releases instead of re-enqueuing,
 losing the wakeup and deadlocking."

Object subclass: #Sink instanceVariableNames: 'count'.
Sink >> initialize count := 0. ^ self.
Sink >> ping count := count + 1. ^ count.

Object subclass: #Driver instanceVariableNames: 'actors'.
Driver >> setActors: a actors := a. ^ self.
Driver >> work
  | sum |
  sum := 0.
  actors doYielding: [ :a | sum := sum + ((a ping) wait) ].
  ^ sum.

actorsList := OrderedCollection new.
1 to: 3 do: [ :i | | s | s := Sink new. s initialize. actorsList add: (s asActor) ].
d := Driver new. d setActors: actorsList.
((d asActor) work) wait.
"3 sinks × 1 ping each, each settles count=1; sum = 1+1+1+1+1+1 = 3+3 = 6"
"Wait — sum is 1+1+1 = 3 per single call. Let me recompute."
"Actually: each sink is fresh, count starts 0, ping returns 1. sum=3."
