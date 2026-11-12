"EXPECT: #('kept' 100 101 6 2 #(10 20 30) 42 #(3 2 3) 7 #(3 2) 11)"
"A receiver that is not a Boolean gets real closures for the block literals
 of an inlined conditional (LANGUAGE.md 6.4). A closure it keeps shares the
 variables of the sending method with that method for as long as either
 lives, as a block that was never inlined would: a write on either side is
 seen by the other, before and after the send returns. Each entry of an
 inlined block with temporaries has its own variables, so closures kept from
 different iterations see different ones; a name resolves to its innermost
 declaration. Until 0.10.0's fix round 1 a kept closure that used a variable
 of the method signalled an Error once the send had returned. The last row
 runs a kept closure on an actor's worker thread."
Object subclass: #Keeper instanceVariableNames: 'kept'.
Keeper >> and: aBlock kept := aBlock. ^ #kept.
Keeper >> runKept ^ kept value.
Object subclass: #NB.
NB >> and: aBlock ^ aBlock.
Object subclass: #Probe.
Probe >> keep: k | x r | x := 5. r := k and: [ x := x + 1 ]. x := 100. ^ { r. x }.
Probe >> keepAndRun: k | x | x := 5. k and: [ x := x + 1 ]. k runKept. ^ x.
Probe >> frameWriteSeen | t b | t := 1. b := NB new and: [ t ]. t := 2. ^ b value.
Probe >> perIteration
  | blks |
  blks := OrderedCollection new.
  1 to: 3 do: [ :i | | t | t := i * 10. blks add: (NB new and: [ t ]) ].
  ^ (blks collect: [ :b | b value ]) asArray.
Probe >> withTemporaries
  | blk |
  1 to: 1 do: [ :i | | t | t := 42. blk := NB new and: [ | u | u := t. u ] ].
  ^ blk value.
Probe >> shadowing
  | t blks |
  t := 1. blks := OrderedCollection new.
  blks add: (NB new and: [ t ]).
  true ifTrue: [ | t | t := 2. blks add: (NB new and: [ t ]) ].
  blks add: (NB new and: [ t ]).
  t := 3.
  ^ (blks collect: [ :b | b value ]) asArray.
Object subclass: #Runner.
Runner >> run: aBlock ^ aBlock value.
Probe >> onAnotherThread
  | x blk |
  x := 1.
  blk := NB new and: [ x := x + 10 ].
  (Runner new asActor run: blk) wait.
  ^ x.
Probe >> argument: a ^ NB new and: [ a + 2 ].
Probe >> nestedWrite
  | x blk |
  x := 0.
  true ifTrue: [ | y | y := 1. blk := NB new and: [ x := x + y. y := y + 1. [ x ] value ] ].
  blk value. blk value.
  ^ { x. 2 }.
| k probe first second |
k := Keeper new.
probe := Probe new.
first := probe keep: k.
second := k runKept.
{ first first. first last. second. probe keepAndRun: Keeper new. probe frameWriteSeen.
  probe perIteration. probe withTemporaries. probe shadowing. (probe argument: 5) value.
  probe nestedWrite. probe onAnotherThread }
