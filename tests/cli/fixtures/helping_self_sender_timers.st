"S48: timers keep settling while waits help under an actor that keeps
 sending itself messages. Two waits nested in ensure: run under a Spinner:
 A waits for (Future after: 200), B (sent while A waits, so with one worker
 it runs on top of A) waits within: 200 for a Future nothing settles. A
 waiter runs a queued task (a timer's settle) before each message it helps
 with, and checks its Future after each, so B gets its TimeoutError, returns,
 and A, under it, resumes with its timer settled. Before 0.16.1, with one
 worker, the helped Spinner turn never ended and neither timer ever settled:
 the program hung. Both waits are running before the Spinner starts (each
 announces it through an Atom the program polls): no fixed pause."
Object subclass: #Spinner instanceVariableNames: 'proxy stop'.
Spinner class >> stop: anAtom
  | s | s := self basicNew. ^ s setProxy: s asActor stop: anAtom.
Spinner >> setProxy: p stop: a
  proxy := p. stop := a.
Spinner >> begin
  proxy spin.
Spinner >> spin
  stop value ifTrue: [^ 'done'].
  proxy spin.
  ^ 'again'.

Object subclass: #Victim.
Victim >> afterTimerAnnouncing: anAtom
  anAtom swap: [:x | true].
  [(Future after: 200) wait] ensure: [].
  ^ 'timer'.
Victim >> withinAnnouncing: anAtom
  anAtom swap: [:x | true].
  ^ [[Future new waitWithin: 200] on: TimeoutError do: [:e | 'timedOut']] ensure: [].

| stop sp startedA startedB a b ra rb |
stop := Atom on: false.
sp := Spinner stop: stop.
startedA := Atom on: false.
startedB := Atom on: false.
a := Victim new asActor afterTimerAnnouncing: startedA.
[startedA value] whileFalse: [Object sleep: 5].
b := Victim new asActor withinAnnouncing: startedB.
[startedB value] whileFalse: [Object sleep: 5].
sp begin.
ra := [a waitWithin: 20000] on: TimeoutError do: [:e | 'buried'].
rb := [b waitWithin: 20000] on: TimeoutError do: [:e | 'buried'].
stop swap: [:x | true].
{ ra. rb } printString displayNl.
