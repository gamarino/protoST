"S46: the deadlock verdict of a chain of nested waits (S40) whose last event
 is the program thread's own park. With one configured worker the pool grows
 to 256 workers, 17 buried links each, and link 4353's turn never starts. The
 link past the first cap resolves Started; the program thread waits for it,
 then waits for the chain. Run with PROTOST_TEST_HOLD_MAIN_PARK=1, the runtime
 holds the program thread just before it marks itself blocked until every
 worker has confirmed the stuck round, had it refused (the program thread was
 not yet blocked) and parked, so the park is the last event, in every run.
 The answer is the start of the deadlock error."
Object subclass: #Link instanceVariableNames: 'next started'.
Link >> next: n started: s
  next := n. started := s.
Link >> ping
  next isNil ifTrue: [^ 1].
  started isNil ifFalse: [started resolve: true].
  #(1) do: [:i | ^ 1 + (next ping wait)].
  ^ 0.

| n links started f |
n := 5000.
started := Future new.
links := (1 to: n) collect: [:i | Link new asActor].
1 to: n - 1 do: [:i | ((links at: i) next: (links at: i + 1) started: (i = 18 ifTrue: [started] ifFalse: [nil])) wait].
f := links first ping.
started wait.
[f wait. 'no error'] on: Error do: [:e | e messageText copyFrom: 1 to: 30]
