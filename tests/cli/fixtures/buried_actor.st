"With one worker: A pauses (so D's ping is queued before B), then waits inside a
 collect: on B and runs other ready messages meanwhile - D's ping first. D
 waits for A, which is buried under D on the same thread and cannot run until
 D returns; no chain of waits reaches D itself. D's wait must report a
 deadlock instead of spinning for ever."
Object subclass: #B.
B >> work  ^ 1.
Object subclass: #A instanceVariableNames: 'b'.
A >> b: x  b := x.
A >> run
  | f |
  f := b work.
  Object sleep: 200.
  ^ #(1) collect: [:x | f wait]
A >> value  ^ 7.
Object subclass: #D instanceVariableNames: 'a'.
D >> a: x  a := x.
D >> ping  ^ [#(1) collect: [:x | (a value) wait]] on: Error do: [:e |
  (e messageText includesSubstring: 'deadlock') ifTrue: ['deadlock'] ifFalse: [e messageText]].

| a b d r |
a := A new asActor. b := B new asActor. d := D new asActor.
(a b: b) wait. (d a: a) wait.
a run.
Object sleep: 100.
r := d ping.
r wait
