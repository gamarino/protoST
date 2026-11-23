"EXPECT: #resumed"
"S48: a wait that helps regains control under an actor that keeps sending
 itself messages. The Victim waits inside ensure: (a wait that cannot
 suspend, so its worker runs other actors' messages on top of it) for a
 Future the program resolves while a Spinner keeps re-sending itself a
 finite message. The waiter checks its own Future after every message it
 helps with, so it resumes while the Spinner still runs. Before 0.16.1 the
 helped turn drained the Spinner's whole mailbox, its self-sends included,
 and never ended: with one worker the whole program stopped, timers
 included; with more, the Victim stayed buried whenever its own worker took
 the Spinner.
 The Victim announces that it is about to wait (an Atom the program polls),
 so the Spinner starts only once the Victim's turn runs: no fixed pause."
Object subclass: #Spinner instanceVariableNames: 'proxy stop count'.
Spinner class >> stop: anAtom
  | s | s := self basicNew. ^ s setProxy: s asActor stop: anAtom.
Spinner >> setProxy: p stop: a
  proxy := p. stop := a. count := 0.
Spinner >> begin
  proxy spin.
Spinner >> spin
  stop value ifTrue: [^ #done].
  count := count + 1.
  proxy spin.
  ^ #again.
Spinner >> count
  ^ count.
Spinner >> proxy
  ^ proxy.

Object subclass: #Victim.
Victim >> nestedWaitFor: aFuture announcing: anAtom
  anAtom swap: [:x | true].
  [aFuture wait] ensure: [].
  ^ #resumed.

| f v stop started sp r spins |
stop := Atom on: false.
started := Atom on: false.
f := Future new.
v := Victim new asActor nestedWaitFor: f announcing: started.
[started value] whileFalse: [Object sleep: 5].
sp := Spinner stop: stop.
sp begin.
"Let the Spinner run for a while on top of the waiting Victim."
(Future after: 100) wait.
f resolve: true.
r := [v waitWithin: 20000] on: TimeoutError do: [:e | #buried].
stop swap: [:x | true].
spins := (sp proxy count) wait.
('spins before the stop: ' , (spins > 0) printString) displayNl.
r printString displayNl.
