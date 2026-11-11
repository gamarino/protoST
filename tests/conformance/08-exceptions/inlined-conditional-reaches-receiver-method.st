"EXPECT: true"
"When the receiver of an inlined conditional (ifTrue:, ifFalse:,
 ifTrue:ifFalse:, ifFalse:ifTrue:, and:, or: with literal blocks) is not a
 Boolean, the message is sent for real, with closures for the block literals
 (0.10.0): a class that defines the selector answers it, and a
 doesNotUnderstand: override gets the whole Message, so a forwarding proxy
 forwards it. The closures share the method's variables while the send runs:
 a write in the block is seen by the method after the send, and a non-local
 return from the block returns from the method. Each row is { inlined. sent },
 where the sent form passes a block held in a variable; the program answers
 whether every pair is equal, and prints the rows on a mismatch."
Object subclass: #Fuzzy.
Fuzzy >> and: aBlock ^ { #and. aBlock value }.
Fuzzy >> or: aBlock ^ { #or. aBlock value }.
Fuzzy >> ifTrue: aBlock ^ { #ifTrue. aBlock value }.
Fuzzy >> ifFalse: aBlock ^ { #ifFalse. aBlock value }.
Fuzzy >> ifTrue: a ifFalse: b ^ { #ifTrueIfFalse. a value. b value }.
Fuzzy >> ifFalse: a ifTrue: b ^ { #ifFalseIfTrue. a value. b value }.
Object subclass: #Proxy instanceVariableNames: 'target'.
Proxy >> target: t target := t.
Proxy >> doesNotUnderstand: m
  ^ { m selector. m arguments size. target perform: m selector withArguments: m arguments }.
Object subclass: #Probe.
Probe >> inlinedWrite: f | x y | x := 10. y := 0. f and: [ y := x + 1 ]. ^ { x. y }.
Probe >> sentWrite: f | x y b | x := 10. y := 0. b := [ y := x + 1 ]. f and: b. ^ { x. y }.
Probe >> inlinedReturn: f f ifTrue: [ ^ #returned ]. ^ #fellThrough.
Probe >> sentReturn: f | b | b := [ ^ #returned ]. f ifTrue: b. ^ #fellThrough.
Probe >> inlinedLoop: f | sum | sum := 0.
  1 to: 3 do: [ :i | sum := sum + (f and: [ i * 10 ]) last ]. ^ sum.
Probe >> sentLoop: f | sum | sum := 0.
  1 to: 3 do: [ :i | | b | b := [ i * 10 ]. sum := sum + (f and: b) last ]. ^ sum.
| f p probe a b c rows ok |
f := Fuzzy new.
p := Proxy new target: true.
probe := Probe new.
a := [ #a ]. b := [ #b ]. c := [ 3 ].
rows := {
  { f and: [ #a ]. f and: a }.
  { f or: [ #a ]. f or: a }.
  { f ifTrue: [ #a ]. f ifTrue: a }.
  { f ifFalse: [ #a ]. f ifFalse: a }.
  { f ifTrue: [ #a ] ifFalse: [ #b ]. f ifTrue: a ifFalse: b }.
  { f ifFalse: [ #a ] ifTrue: [ #b ]. f ifFalse: a ifTrue: b }.
  { p and: [ 3 ]. p and: c }.
  { p ifTrue: [ 3 ] ifFalse: [ #b ]. p ifTrue: c ifFalse: b }.
  { probe inlinedWrite: f. probe sentWrite: f }.
  { probe inlinedReturn: f. probe sentReturn: f }.
  { probe inlinedLoop: f. probe sentLoop: f } }.
ok := rows allSatisfy: [ :r | r first = r last ].
ok ifFalse: [ rows printNl ].
ok
