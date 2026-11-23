"S48: a wait that helps runs one message per turn, and the actor's order and
 single runner are kept. A Log actor receives 500 record: messages while the
 only busy worker is a Victim waiting inside ensure: (so, with one worker,
 every record: runs as a helped turn of one message, the rest of the batch
 parked ahead of the queue). Every record: checks that no other turn of the
 Log runs at the same time, and the entries must come back in send order.
 The Log also sends itself a note: after every 100th record; the notes must
 land after the records already queued when each was sent, as a mailbox is
 FIFO."
Object subclass: #Log instanceVariableNames: 'proxy entries running overlap'.
Log class >> new
  | l | l := self basicNew. ^ l setProxy: l asActor.
Log >> setProxy: p
  proxy := p. entries := OrderedCollection new. running := 0. overlap := false.
Log >> proxy
  ^ proxy.
Log >> record: i
  running := running + 1.
  running > 1 ifTrue: [overlap := true].
  entries add: i.
  i \\ 100 = 0 ifTrue: [proxy note: i].
  running := running - 1.
  ^ i.
Log >> note: i
  entries add: (Array with: #note with: i).
  ^ i.
Log >> report
  ^ Array with: overlap with: entries asArray.

Object subclass: #Victim.
Victim >> nestedWaitFor: aFuture announcing: anAtom
  anAtom swap: [:x | true].
  [aFuture wait] ensure: [].
  ^ #resumed.

| log gate started v last report entries expected ok |
log := Log new proxy.
gate := Future new.
started := Atom on: false.
v := Victim new asActor nestedWaitFor: gate announcing: started.
[started value] whileFalse: [Object sleep: 5].
1 to: 500 do: [:i | last := log record: i].
last wait.
gate resolve: true.
(v waitWithin: 20000) printString displayNl.
report := log report wait.
entries := report at: 2.
"Records 1..500 in order; each note: i after record i, before or after later
 records, never before record i."
expected := (1 to: 500) asArray.
ok := (entries reject: [:e | e isArray]) asArray = expected.
1 to: 5 do: [:k |
  | at noteAt |
  at := entries indexOf: k * 100.
  noteAt := entries indexOf: (Array with: #note with: k * 100).
  (noteAt > at) ifFalse: [ok := false]].
('overlap: ' , (report at: 1) printString) displayNl.
('order kept: ' , ok printString) displayNl.
