"EXPECT: true"
"ifNil:, ifNotNil:, ifNil:ifNotNil: and ifNotNil:ifNil: with literal blocks
 are compiled inline (0.10.0) and answer what the send answers: ifNil: the
 receiver when it is not nil, ifNotNil: nil when the receiver is nil, else
 the value of the arm that runs; an ifNotNil: arm may take the receiver as
 its argument or take none. An argument captured by an inner block, a
 non-local return from an arm and an argument that hides an outer variable
 behave as with the send. Each row is { inlined. sent }, the sent form
 passing blocks held in variables; the program answers whether every pair
 is equal."
Object subclass: #Probe.
Probe >> early: x x ifNil: [ ^ #wasNil ]. ^ #notNil.
Probe >> earlySent: x | b | b := [ ^ #wasNil ]. x ifNil: b. ^ #notNil.
| a nilArm one zero maker y rows ok |
nilArm := [ #none ].
one := [ :v | v + 1 ].
zero := [ #some ].
y := 100.
rows := {
  { 3 ifNil: [ #none ]. 3 ifNil: nilArm }.
  { nil ifNil: [ #none ]. nil ifNil: nilArm }.
  { 3 ifNotNil: [ :v | v + 1 ]. 3 ifNotNil: one }.
  { 3 ifNotNil: [ #some ]. 3 ifNotNil: zero }.
  { nil ifNotNil: [ :v | v + 1 ]. nil ifNotNil: one }.
  { 3 ifNil: [ #none ] ifNotNil: [ :v | v + 1 ]. 3 ifNil: nilArm ifNotNil: one }.
  { nil ifNil: [ #none ] ifNotNil: [ :v | v + 1 ]. nil ifNil: nilArm ifNotNil: one }.
  { 3 ifNotNil: [ :v | v + 1 ] ifNil: [ #none ]. 3 ifNotNil: one ifNil: nilArm }.
  { nil ifNotNil: [ :v | v + 1 ] ifNil: [ #none ]. nil ifNotNil: one ifNil: nilArm }.
  { (3 ifNotNil: [ :y | y * 2 ]) + y. (3 ifNotNil: [ :v | v * 2 ]) + 100 }.
  { (4 ifNotNil: [ :v | [ v * 10 ] ]) value. 40 }.
  { Probe new early: nil. Probe new earlySent: nil }.
  { Probe new early: 3. Probe new earlySent: 3 }.
  { [ a := 0. 1 to: 3 do: [ :i | (i odd ifTrue: [ i ]) ifNotNil: [ :v | a := a + v ] ]. a ] value. 4 } }.
ok := rows allSatisfy: [ :r | r first = r last ].
ok ifFalse: [ rows printNl ].
ok
