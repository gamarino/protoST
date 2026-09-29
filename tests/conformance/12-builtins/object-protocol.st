"EXPECT: true false true true 7 7 caught copy-ok nil-ok true true"
"The everyday Object protocol (0.4.0 kernel)."
Object subclass: #W instanceVariableNames: 'v'.
W >> v: x
  v := x.
  ^ self.

W >> v
  ^ v.

W >> abstract
  ^ self subclassResponsibility.

w := W new v: 5.
b := [:x | x ifTrue: ['true'] ifFalse: ['false']].
r1 := b value: (3 isKindOf: Number).
r2 := b value: (3 isMemberOf: Number).
r3 := b value: (w respondsTo: #v).
r4 := b value: (w respondsTo: #abstract).
r5 := (3 perform: #+ with: 4) printString.
r6 := (3 perform: #+ withArguments: #(4)) printString.
r7 := [w abstract] on: Error do: [:e | 'caught'].
c := w copy. c v: 9.
r8 := (w v = 5 and: [c v = 9]) ifTrue: ['copy-ok'] ifFalse: ['copy-bad'].
r9 := (nil isNil and: [3 notNil]) ifTrue: ['nil-ok'] ifFalse: ['nil-bad'].
r10 := b value: (W superclass == Object).
r11 := b value: ((w respondsTo: #frobnicate) not and: [(3 perform: #factorial) = 6]).
r1 , ' ' , r2 , ' ' , r3 , ' ' , r4 , ' ' , r5 , ' ' , r6 , ' ' , r7 , ' ' , r8 , ' ' , r9 , ' ' , r10 , ' ' , r11.
