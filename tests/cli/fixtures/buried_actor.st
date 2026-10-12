"With one worker: A starts, sets a flag the main program waits for, and sleeps
 (so D's ping is queued while A runs, on top of B's work), then waits inside a
 collect: on B and runs other ready messages meanwhile - D's ping first. D
 waits for A, which is buried under D on the same thread and cannot run until
 D returns; no chain of waits reaches D itself. D's wait must report a
 deadlock instead of spinning for ever.
 The flag replaces a fixed 100 ms pause of the main program: when the worker
 started A's run later than that (seen once on a macOS CI runner), D's ping
 was ready first, ran before A was buried, and correctly answered #(7)."
Object subclass: #B.
B >> work  ^ 1.
Object subclass: #A instanceVariableNames: 'b'.
A >> b: x  b := x.
A >> run: started
  | f |
  f := b work.
  started at: 1 put: true.
  Object sleep: 500.
  ^ #(1) collect: [:x | f wait]
A >> value  ^ 7.
Object subclass: #D instanceVariableNames: 'a'.
D >> a: x  a := x.
D >> ping  ^ [#(1) collect: [:x | (a value) wait]] on: Error do: [:e |
  (e messageText includesSubstring: 'deadlock') ifTrue: ['deadlock'] ifFalse: [e messageText]].

| a b d r started |
a := A new asActor. b := B new asActor. d := D new asActor.
(a b: b) wait. (d a: a) wait.
started := Array new: 1.
a run: started.
[(started at: 1) == true] whileFalse: [Object sleep: 5].
r := d ping.
r wait
