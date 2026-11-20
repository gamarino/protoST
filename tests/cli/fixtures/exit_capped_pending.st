"S45: a program may end while a wait nested in a block past the helping cap
 (S40) is still pending; the runtime finishes it before it exits. With one
 configured worker, links 1-17 of an 18-link chain are buried on worker 0, so
 link 17's wait for link 18 is at the cap. Link 18 runs on the worker the pool
 adds, resolves Started (so the program thread knows worker 0 is capped), and
 yields on a Future a timer settles. The optional argument `pause` makes the
 program end with the pool paused (WorkerPool stopProcessing); `gate` also
 waits for a timer after pausing, which wakes the free worker into the pause
 gate. The answer is 'main done', followed at exit by 'last link resumed'.
 The timers only shape the order of events: every order must finish."
Object subclass: #Link instanceVariableNames: 'next started'.
Link >> next: n started: s
  next := n. started := s.
Link >> ping
  next isNil ifTrue: [
    started resolve: true.
    (Future after: 200) wait.
    Transcript showCr: 'last link resumed'.
    ^ 1].
  #(1) do: [:i | ^ 1 + (next ping wait)].
  ^ 0.

| links started mode |
mode := Smalltalk arguments isEmpty ifTrue: ['plain'] ifFalse: [Smalltalk arguments first].
started := Future new.
links := (1 to: 18) collect: [:i | Link new asActor].
1 to: 18 do: [:i | ((links at: i) next: (i < 18 ifTrue: [links at: i + 1] ifFalse: [nil]) started: started) wait].
links first ping.
started wait.
mode = 'plain' ifFalse: [WorkerPool stopProcessing].
mode = 'gate' ifTrue: [(Future after: 1) wait].
'main done'
