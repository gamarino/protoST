"EXPECT: #('early' 7 'late' 2 'found' 'none' 9)"
"A ^ inside the literal block of an inlined and: / or: returns from the home
 method: directly in the method, from an inlined and: inside a real block
 (do:) and inside a block that another method evaluates, and only when the
 block runs (short-circuit leaves the method running)."
Object subclass: #R.
R >> direct: x
  (x > 0 and: [ ^ #early ]) ifFalse: [ ^ 7 ].
  ^ #unreached
R >> viaOr: x
  (x > 0 or: [ ^ #late ]).
  ^ x
R >> find: aCollection
  aCollection do: [ :e | (e isString and: [ ^ #found ]) ].
  ^ #none
R >> through: aBlock
  ^ aBlock value
R >> deep: x
  self through: [ (x > 5 and: [ ^ x ]) or: [ ^ 0 ] ].
  ^ #unreached
| r |
r := R new.
{ r direct: 1. r direct: 0. r viaOr: 0. r viaOr: 2.
  r find: #(1 'a' 3). r find: #(1 2). r deep: 9 }
